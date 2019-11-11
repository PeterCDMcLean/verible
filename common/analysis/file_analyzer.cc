// Copyright 2017-2019 The Verible Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation of FileAnalyzer methods.

#include "common/analysis/file_analyzer.h"

#include <sstream>  // IWYU pragma: keep  // for ostringstream
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "common/lexer/lexer.h"
#include "common/parser/parse.h"
#include "common/text/concrete_syntax_tree.h"
#include "common/text/line_column_map.h"
#include "common/text/text_structure.h"
#include "common/text/token_info.h"
#include "common/text/token_stream_view.h"
#include "common/util/status.h"

namespace verible {

// Translates phase enum into string for diagnostic messages.
static const char* AnalysisPhaseName(const AnalysisPhase& phase) {
  switch (phase) {
    case AnalysisPhase::kLexPhase:
      return "lexical";
    case AnalysisPhase::kPreprocessPhase:
      return "preprocessing";
    case AnalysisPhase::kParsePhase:
      return "syntax";
    default:
      return "UNKNOWN";
  }
}

static std::string GetHelpTopicUrl(absl::string_view topic) {
  return "https://github.com/google/verible";
}

std::ostream& operator<<(std::ostream& stream, const AnalysisPhase& phase) {
  return stream << AnalysisPhaseName(phase);
}

// Grab tokens until EOF, and initialize a stream view with all tokens.
util::Status FileAnalyzer::Tokenize(Lexer* lexer) {
  // TODO(fangism): provide a Lexer interface to grab all tokens en masse,
  // which would save virtual function dispatch overhead.
  lexer->Restart(Data().Contents());
  TokenSequence& tokens = MutableData().MutableTokenStream();
  do {
    const auto& new_token = lexer->DoNextToken();
    tokens.push_back(new_token);
    if (lexer->TokenIsError(new_token)) {  // one more virtual function call
      VLOG(1) << "Lexical error with token: " << new_token;
      rejected_tokens_.push_back(
          RejectedToken{new_token, AnalysisPhase::kLexPhase,
                        "" /* no detailed explanation */});
      // Stop-on-first-error.  Error details are in the RejectedToken.
      return util::InvalidArgumentError("Lexical error.");
    }
  } while (!tokens.back().isEOF());
  // Final token is EOF.
  // Force EOF token's text range to be empty, pointing to end of original
  // string.  Otherwise, its range ends up overlapping with the previous token.
  tokens.back() = Data().EOFToken();

  // Partition token stream into line-by-line slices.
  MutableData().CalculateFirstTokensPerLine();

  // Initialize filtered view of token stream.
  InitTokenStreamView(tokens, &MutableData().MutableTokenStreamView());
  return util::OkStatus();
}

// Runs the parser on the current TokenStreamView.
util::Status FileAnalyzer::Parse(Parser* parser) {
  const util::Status status = parser->Parse();
  // Transfer syntax tree root, even if there were (recovered) syntax errors,
  // because the partial tree can still be useful to analyze.
  MutableData().MutableSyntaxTree() = parser->TakeRoot();
  if (status.ok()) {
    CHECK(SyntaxTree().get()) << "Expected syntax tree from parsing \""
                              << filename_ << "\", but got none.";
  } else {
    for (const auto& token : parser->RejectedTokens()) {
      rejected_tokens_.push_back(RejectedToken{
          token, AnalysisPhase::kParsePhase, "" /* no detailed explanation */});
    }
  }
  return status;
}

// Reports human-readable token error.
std::string FileAnalyzer::TokenErrorMessage(
    const TokenInfo& error_token) const {
  // TODO(fangism): accept a RejectedToken to get an explanation message.
  const LineColumnMap& line_column_map = Data().GetLineColumnMap();
  const absl::string_view base_text = Data().Contents();
  std::ostringstream output_stream;
  if (!error_token.isEOF()) {
    const auto left = line_column_map(error_token.left(base_text));
    auto right = line_column_map(error_token.right(base_text));
    --right.column;  // Point to last character, not one-past-the-end.
    output_stream << "token: \"" << error_token.text << "\" at " << left;
    if (left.line == right.line) {
      // Only print upper bound if it differs by > 1 character.
      if (left.column + 1 < right.column) {
        // .column is 0-based index, so +1 to get 1-based index.
        output_stream << '-' << right.column + 1;
      }
    } else {
      // Already prints 1-based index.
      output_stream << '-' << right;
    }
  } else {
    const auto end = line_column_map(base_text.length());
    output_stream << "token: <<EOF>> at " << end;
  }
  return output_stream.str();
}

std::vector<std::string> FileAnalyzer::TokenErrorMessages() const {
  std::vector<std::string> messages;
  messages.reserve(rejected_tokens_.size());
  for (const auto& rejected_token : rejected_tokens_) {
    messages.push_back(TokenErrorMessage(rejected_token.token_info));
  }
  return messages;
}

// Synchronize with 'VerilogLint' regex in glint.cfg.
std::string FileAnalyzer::LinterTokenErrorMessage(
    const RejectedToken& error_token) const {
  const LineColumnMap& line_column_map = Data().GetLineColumnMap();
  const absl::string_view base_text = Data().Contents();
  std::ostringstream output_stream;
  output_stream << filename_ << ':';
  if (!error_token.token_info.isEOF()) {
    const auto left = line_column_map(error_token.token_info.left(base_text));
    output_stream << left << ": " << error_token.phase << " error, rejected \""
                  << error_token.token_info.text << "\" ("
                  << GetHelpTopicUrl("syntax-error") << ").";
  } else {
    const int file_size = base_text.length();
    const auto end = line_column_map(file_size);
    output_stream << end << ": " << error_token.phase
                  << " error (unexpected EOF) ("
                  << GetHelpTopicUrl("syntax-error") << ").";
  }
  // TODO(b/63893567): Explain syntax errors by inspecting state stack.
  if (!error_token.explanation.empty()) {
    output_stream << "  " << error_token.explanation;
  }
  return output_stream.str();
}

std::vector<std::string> FileAnalyzer::LinterTokenErrorMessages() const {
  std::vector<std::string> messages;
  messages.reserve(rejected_tokens_.size());
  for (const auto& rejected_token : rejected_tokens_) {
    messages.push_back(LinterTokenErrorMessage(rejected_token));
  }
  return messages;
}

}  // namespace verible

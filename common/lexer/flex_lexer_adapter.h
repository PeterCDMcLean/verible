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

// FlexLexerAdapter class adapts Flex-generated lexers to Lexer interface.
//
// Template parameter L must be a Flex-generated lexer (yyFlexLexer).
// The adapter inherits from this generated class to override functions.
//
// Main lexing function yylex() must be defined in a subclass.
//
// Example usage:
// in verilog_lexer.h:
// class verilogFlexLexer;  // generated by flex
// class VerilogLexer : public verible::FlexLexerAdapter<verilogFlexLexer> {
//   ...
// };
//
// and in verilog.lex:
// %option yyclass="verilog::VerilogLexer"

#ifndef VERIBLE_COMMON_LEXER_FLEX_LEXER_ADAPTER_H_
#define VERIBLE_COMMON_LEXER_FLEX_LEXER_ADAPTER_H_

#include <cstdlib>
#include <iostream>
#include <sstream>  // IWYU pragma: keep  // for ostringstream
#include <string>

#include "absl/strings/string_view.h"
#include "common/lexer/lexer.h"
#include "common/text/token_info.h"
#include "common/util/logging.h"

namespace verible {

// The "L" base class of FlexLexerAdaptor needs to use code_stream_ in its
// constructor, which means that code_stream_ must be initialized first.  All
// base classes are initialized before any non-static data members, so to
// achieve that, we need to also put code_stream_ in a base class that is
// ordered before "L" in FlexLexerAdaptor's base classes.
class CodeStreamHolder {
 protected:
  // The stream object conforms to the FlexLexer input interface.
  // Even though scanning is done on the stream's internal copy of the input
  // string, the byte offsets being tracked can be used to construct
  // string_views based on the original string's start address.
  // Using the standard istream interface also lets us switch buffers, e.g.
  // during preprocessing.
  std::istringstream code_stream_;
};

// L is a (flex-generated) yyFlexLexer-like class.
template <typename L>
class FlexLexerAdapter : private CodeStreamHolder, protected L, public Lexer {
 public:
  explicit FlexLexerAdapter(absl::string_view code)
      : L(&code_stream_),
        code_(code),
        // last_token_ points to the beginning of the code_ buffer
        last_token_(0 /* enum doesn't matter */, code_.substr(0, 0)) {
    code_stream_.str(std::string(code));
    // istringstream copies text into its own internal buffer.
  }

  // Returns the token associated with the last UpdateLocation() call.
  const TokenInfo& GetLastToken() const override { return last_token_; }

  // Returns next token and updates its location.
  const TokenInfo& DoNextToken() override {
    last_token_.token_enum = this->yylex();
    // yylex has already called UpdateLocation()
    return last_token_;
  }

 protected:
  // Must be called by subclasses to update location of the current token.
  void UpdateLocation() { last_token_.AdvanceText(this->YYLeng()); }

  // Restart lexer by pointing to new input stream, and reset all state.
  void Restart(absl::string_view code) override {
    code_ = code;
    code_stream_.str(std::string(code_));
    last_token_ = TokenInfo(0, code_.substr(0, 0));

    // Reset buffer stack.
    while (L::yy_buffer_stack_top > 1) {  // Keep bottom buffer only.
      L::yypop_buffer_state();
    }

    // Reset the current buffer to use new stream.
    L::yyrestart(&code_stream_);

    // Reset start condition stack.
    while (L::yy_start_stack_ptr > 1) {  // Keep INITIAL state.
      L::yy_pop_state();
    }
  }

  // Overrides yyFlexLexer's implementation to handle unrecognized chars.
  virtual void LexerOutput(const char* buf, int size) {
    VLOG(1) << "LexerOutput: rejected text: \"" << std::string(buf, size)
            << '\"';

    // Update location by the size of the unrecognized sequence.
    // Note, this is a last-resort guard. The preferred way
    // to handle unrecognized chars is to add wildcard rule
    // at the end of the lexer definition that just calls
    // UpdateLocation().
    last_token_.AdvanceText(size);
    // TODO(fangism): Communicate some sort of error token to the consumer.
  }

  // Overrides yyFlexLexer's implementation to do proper error handling.
  virtual void LexerError(const char* msg) {
    std::cerr << "Fatal LexerError: " << msg;
    abort();
  }

 private:
  // A read-only view of the entire text to be scanned.
  absl::string_view code_;

  // Contains the enumeration and the substring slice of the last lexed token.
  TokenInfo last_token_;
};

}  // namespace verible

#endif  // VERIBLE_COMMON_LEXER_FLEX_LEXER_ADAPTER_H_

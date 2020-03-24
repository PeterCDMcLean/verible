// Copyright 2017-2020 The Verible Authors.
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

#include "common/strings/obfuscator.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "common/util/logging.h"

namespace verible {

bool Obfuscator::encode(absl::string_view key, absl::string_view value) {
  return translator_.insert(std::string(key), std::string(value));
}

absl::string_view Obfuscator::operator()(absl::string_view input) {
  if (decode_mode) {
    const auto p = translator_.find_reverse(input);  // hetergeneous lookup
    return (p != nullptr) ? *p : input;
  } else {
    const std::string* str = translator_.insert_using_value_generator(
        std::string(input), [=]() { return generator_(input); });
    return *str;
  }
}

constexpr char kPairSeparator = ' ';

std::string Obfuscator::save() const {
  std::ostringstream stream;
  for (const auto& pair : translator_.forward_view()) {
    stream << pair.first << kPairSeparator << *pair.second << std::endl;
  }
  return stream.str();
}

absl::Status Obfuscator::load(absl::string_view mapping) {
  const std::vector<absl::string_view> lines =
      absl::StrSplit(mapping, '\n', absl::SkipEmpty());
  for (const auto& line : lines) {
    const std::vector<absl::string_view> elements =
        absl::StrSplit(line, kPairSeparator);
    if (elements.size() < 2) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to parse line:\n", line));
    }
    // elements are expected to be identifiers (original, obfuscated)
    encode(elements[0], elements[1]);
  }
  return absl::OkStatus();
}

bool IdentifierObfuscator::encode(absl::string_view key,
                                  absl::string_view value) {
  CHECK_EQ(key.length(), value.length());
  return parent_type::encode(key, value);
}

}  // namespace verible

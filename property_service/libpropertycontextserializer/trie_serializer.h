//
// Copyright (C) 2017 The Android Open Source Project
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
//

#ifndef PROPERTY_CONTEXT_SERIALIZER_TRIE_SERIALIZER_H
#define PROPERTY_CONTEXT_SERIALIZER_TRIE_SERIALIZER_H

#include <string>
#include <vector>

#include "property_context_parser/property_context_parser.h"

#include "trie_builder.h"
#include "trie_node_arena.h"

namespace android {
namespace properties {

// A serialized Trie consists of the following
// kMagic  ?
// Version ?
// Offset to ContextsArray
// Offset to RootNode of Trie
class TrieSerializer {
 public:
  TrieSerializer();

  std::string SerializeTrie(const TrieBuilderNode& builder_root, const std::set<std::string>& contexts);

 private:
  uint32_t SerializeContexts(const std::set<std::string>& contexts);
  template <auto size_array, auto match_offset_array, auto context_offset_array>
  void WriteMatchArray(const std::vector<std::pair<std::string, const std::string*>>& sorted_matches,
                       TrieNode* trie_node);
  void WriteTriePrefixMatches(const TrieBuilderNode& builder_node, TrieNode* trie_node);
  void WriteTrieExactMatches(const TrieBuilderNode& builder_node, TrieNode* trie_node);

  // Creates a new TrieNode within arena, and recursively creates its children.
  // Returns the offset within arena.
  uint32_t CreateTrieNode(const TrieBuilderNode& builder_node);

  std::unique_ptr<TrieNodeArena> arena_;
  std::unique_ptr<Contexts> contexts_;
};

}  // namespace properties
}  // namespace android

#endif

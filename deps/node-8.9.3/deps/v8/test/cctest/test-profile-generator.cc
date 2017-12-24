// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Tests of profiles generator and utilities.

#include "include/v8-profiler.h"
#include "src/api.h"
#include "src/objects-inl.h"
#include "src/profiler/cpu-profiler.h"
#include "src/profiler/profile-generator-inl.h"
#include "src/v8.h"
#include "test/cctest/cctest.h"
#include "test/cctest/profiler-extension.h"

using i::CodeEntry;
using i::CodeMap;
using i::CpuProfile;
using i::CpuProfiler;
using i::CpuProfilesCollection;
using i::ProfileNode;
using i::ProfileTree;
using i::ProfileGenerator;
using i::TickSample;
using i::Vector;


TEST(ProfileNodeFindOrAddChild) {
  CcTest::InitializeVM();
  ProfileTree tree(CcTest::i_isolate());
  ProfileNode* node = tree.root();
  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, "aaa");
  ProfileNode* childNode1 = node->FindOrAddChild(&entry1);
  CHECK(childNode1);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, "bbb");
  ProfileNode* childNode2 = node->FindOrAddChild(&entry2);
  CHECK(childNode2);
  CHECK_NE(childNode1, childNode2);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node->FindOrAddChild(&entry2));
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, "ccc");
  ProfileNode* childNode3 = node->FindOrAddChild(&entry3);
  CHECK(childNode3);
  CHECK_NE(childNode1, childNode3);
  CHECK_NE(childNode2, childNode3);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node->FindOrAddChild(&entry2));
  CHECK_EQ(childNode3, node->FindOrAddChild(&entry3));
}


TEST(ProfileNodeFindOrAddChildForSameFunction) {
  CcTest::InitializeVM();
  const char* aaa = "aaa";
  ProfileTree tree(CcTest::i_isolate());
  ProfileNode* node = tree.root();
  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, aaa);
  ProfileNode* childNode1 = node->FindOrAddChild(&entry1);
  CHECK(childNode1);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry1));
  // The same function again.
  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, aaa);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry2));
  // Now with a different security token.
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, aaa);
  CHECK_EQ(childNode1, node->FindOrAddChild(&entry3));
}


namespace {

class ProfileTreeTestHelper {
 public:
  explicit ProfileTreeTestHelper(const ProfileTree* tree)
      : tree_(tree) { }

  ProfileNode* Walk(CodeEntry* entry1,
                    CodeEntry* entry2 = NULL,
                    CodeEntry* entry3 = NULL) {
    ProfileNode* node = tree_->root();
    node = node->FindChild(entry1);
    if (node == NULL) return NULL;
    if (entry2 != NULL) {
      node = node->FindChild(entry2);
      if (node == NULL) return NULL;
    }
    if (entry3 != NULL) {
      node = node->FindChild(entry3);
    }
    return node;
  }

 private:
  const ProfileTree* tree_;
};

}  // namespace


TEST(ProfileTreeAddPathFromEnd) {
  CcTest::InitializeVM();
  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, "bbb");
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, "ccc");
  ProfileTree tree(CcTest::i_isolate());
  ProfileTreeTestHelper helper(&tree);
  CHECK(!helper.Walk(&entry1));
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));

  CodeEntry* path[] = {NULL, &entry3, NULL, &entry2, NULL, NULL, &entry1, NULL};
  std::vector<CodeEntry*> path_vec(path, path + arraysize(path));
  tree.AddPathFromEnd(path_vec);
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));
  ProfileNode* node1 = helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(0u, node1->self_ticks());
  CHECK(!helper.Walk(&entry1, &entry1));
  CHECK(!helper.Walk(&entry1, &entry3));
  ProfileNode* node2 = helper.Walk(&entry1, &entry2);
  CHECK(node2);
  CHECK_NE(node1, node2);
  CHECK_EQ(0u, node2->self_ticks());
  CHECK(!helper.Walk(&entry1, &entry2, &entry1));
  CHECK(!helper.Walk(&entry1, &entry2, &entry2));
  ProfileNode* node3 = helper.Walk(&entry1, &entry2, &entry3);
  CHECK(node3);
  CHECK_NE(node1, node3);
  CHECK_NE(node2, node3);
  CHECK_EQ(1u, node3->self_ticks());

  tree.AddPathFromEnd(path_vec);
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0u, node1->self_ticks());
  CHECK_EQ(0u, node2->self_ticks());
  CHECK_EQ(2u, node3->self_ticks());

  CodeEntry* path2[] = {&entry2, &entry2, &entry1};
  std::vector<CodeEntry*> path2_vec(path2, path2 + arraysize(path2));
  tree.AddPathFromEnd(path2_vec);
  CHECK(!helper.Walk(&entry2));
  CHECK(!helper.Walk(&entry3));
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK(!helper.Walk(&entry1, &entry1));
  CHECK(!helper.Walk(&entry1, &entry3));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK(!helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(2u, node3->self_ticks());
  ProfileNode* node4 = helper.Walk(&entry1, &entry2, &entry2);
  CHECK(node4);
  CHECK_NE(node3, node4);
  CHECK_EQ(1u, node4->self_ticks());
}


TEST(ProfileTreeCalculateTotalTicks) {
  CcTest::InitializeVM();
  ProfileTree empty_tree(CcTest::i_isolate());
  CHECK_EQ(0u, empty_tree.root()->self_ticks());
  empty_tree.root()->IncrementSelfTicks();
  CHECK_EQ(1u, empty_tree.root()->self_ticks());

  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, "aaa");
  CodeEntry* e1_path[] = {&entry1};
  std::vector<CodeEntry*> e1_path_vec(e1_path, e1_path + arraysize(e1_path));

  ProfileTree single_child_tree(CcTest::i_isolate());
  single_child_tree.AddPathFromEnd(e1_path_vec);
  single_child_tree.root()->IncrementSelfTicks();
  CHECK_EQ(1u, single_child_tree.root()->self_ticks());
  ProfileTreeTestHelper single_child_helper(&single_child_tree);
  ProfileNode* node1 = single_child_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(1u, single_child_tree.root()->self_ticks());
  CHECK_EQ(1u, node1->self_ticks());

  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, "bbb");
  CodeEntry* e2_e1_path[] = {&entry2, &entry1};
  std::vector<CodeEntry*> e2_e1_path_vec(e2_e1_path,
                                         e2_e1_path + arraysize(e2_e1_path));

  ProfileTree flat_tree(CcTest::i_isolate());
  ProfileTreeTestHelper flat_helper(&flat_tree);
  flat_tree.AddPathFromEnd(e1_path_vec);
  flat_tree.AddPathFromEnd(e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  flat_tree.AddPathFromEnd(e2_e1_path_vec);
  // Results in {root,0,0} -> {entry1,0,2} -> {entry2,0,3}
  CHECK_EQ(0u, flat_tree.root()->self_ticks());
  node1 = flat_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(2u, node1->self_ticks());
  ProfileNode* node2 = flat_helper.Walk(&entry1, &entry2);
  CHECK(node2);
  CHECK_EQ(3u, node2->self_ticks());
  // Must calculate {root,5,0} -> {entry1,5,2} -> {entry2,3,3}
  CHECK_EQ(0u, flat_tree.root()->self_ticks());
  CHECK_EQ(2u, node1->self_ticks());

  CodeEntry* e2_path[] = {&entry2};
  std::vector<CodeEntry*> e2_path_vec(e2_path, e2_path + arraysize(e2_path));
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, "ccc");
  CodeEntry* e3_path[] = {&entry3};
  std::vector<CodeEntry*> e3_path_vec(e3_path, e3_path + arraysize(e3_path));

  ProfileTree wide_tree(CcTest::i_isolate());
  ProfileTreeTestHelper wide_helper(&wide_tree);
  wide_tree.AddPathFromEnd(e1_path_vec);
  wide_tree.AddPathFromEnd(e1_path_vec);
  wide_tree.AddPathFromEnd(e2_e1_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e2_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  wide_tree.AddPathFromEnd(e3_path_vec);
  // Results in            -> {entry1,0,2} -> {entry2,0,1}
  //            {root,0,0} -> {entry2,0,3}
  //                       -> {entry3,0,4}
  CHECK_EQ(0u, wide_tree.root()->self_ticks());
  node1 = wide_helper.Walk(&entry1);
  CHECK(node1);
  CHECK_EQ(2u, node1->self_ticks());
  ProfileNode* node1_2 = wide_helper.Walk(&entry1, &entry2);
  CHECK(node1_2);
  CHECK_EQ(1u, node1_2->self_ticks());
  node2 = wide_helper.Walk(&entry2);
  CHECK(node2);
  CHECK_EQ(3u, node2->self_ticks());
  ProfileNode* node3 = wide_helper.Walk(&entry3);
  CHECK(node3);
  CHECK_EQ(4u, node3->self_ticks());
  // Calculates             -> {entry1,3,2} -> {entry2,1,1}
  //            {root,10,0} -> {entry2,3,3}
  //                        -> {entry3,4,4}
  CHECK_EQ(0u, wide_tree.root()->self_ticks());
  CHECK_EQ(2u, node1->self_ticks());
  CHECK_EQ(1u, node1_2->self_ticks());
  CHECK_EQ(3u, node2->self_ticks());
  CHECK_EQ(4u, node3->self_ticks());
}


static inline i::Address ToAddress(int n) {
  return reinterpret_cast<i::Address>(n);
}


TEST(CodeMapAddCode) {
  CodeMap code_map;
  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, "bbb");
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, "ccc");
  CodeEntry entry4(i::CodeEventListener::FUNCTION_TAG, "ddd");
  code_map.AddCode(ToAddress(0x1500), &entry1, 0x200);
  code_map.AddCode(ToAddress(0x1700), &entry2, 0x100);
  code_map.AddCode(ToAddress(0x1900), &entry3, 0x50);
  code_map.AddCode(ToAddress(0x1950), &entry4, 0x10);
  CHECK(!code_map.FindEntry(0));
  CHECK(!code_map.FindEntry(ToAddress(0x1500 - 1)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500 + 0x100)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500 + 0x200 - 1)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700 + 0x50)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700 + 0x100 - 1)));
  CHECK(!code_map.FindEntry(ToAddress(0x1700 + 0x100)));
  CHECK(!code_map.FindEntry(ToAddress(0x1900 - 1)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1900)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1900 + 0x28)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950 + 0x7)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950 + 0x10 - 1)));
  CHECK(!code_map.FindEntry(ToAddress(0x1950 + 0x10)));
  CHECK(!code_map.FindEntry(ToAddress(0xFFFFFFFF)));
}


TEST(CodeMapMoveAndDeleteCode) {
  CodeMap code_map;
  CodeEntry entry1(i::CodeEventListener::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::CodeEventListener::FUNCTION_TAG, "bbb");
  code_map.AddCode(ToAddress(0x1500), &entry1, 0x200);
  code_map.AddCode(ToAddress(0x1700), &entry2, 0x100);
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700)));
  code_map.MoveCode(ToAddress(0x1500), ToAddress(0x1700));  // Deprecate bbb.
  CHECK(!code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1700)));
  CodeEntry entry3(i::CodeEventListener::FUNCTION_TAG, "ccc");
  code_map.AddCode(ToAddress(0x1750), &entry3, 0x100);
  CHECK(!code_map.FindEntry(ToAddress(0x1700)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1750)));
}


namespace {

class TestSetup {
 public:
  TestSetup()
      : old_flag_prof_browser_mode_(i::FLAG_prof_browser_mode) {
    i::FLAG_prof_browser_mode = false;
  }

  ~TestSetup() {
    i::FLAG_prof_browser_mode = old_flag_prof_browser_mode_;
  }

 private:
  bool old_flag_prof_browser_mode_;
};

}  // namespace

TEST(RecordTickSample) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfilesCollection profiles(isolate);
  CpuProfiler profiler(isolate);
  profiles.set_cpu_profiler(&profiler);
  profiles.StartProfiling("", false);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = new CodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry* entry2 = new CodeEntry(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry* entry3 = new CodeEntry(i::Logger::FUNCTION_TAG, "ccc");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);
  generator.code_map()->AddCode(ToAddress(0x1700), entry2, 0x100);
  generator.code_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //      -> aaa         - sample1
  //  aaa -> bbb -> ccc  - sample2
  //      -> ccc -> aaa  - sample3
  TickSample sample1;
  sample1.pc = ToAddress(0x1600);
  sample1.tos = ToAddress(0x1500);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);
  TickSample sample2;
  sample2.pc = ToAddress(0x1925);
  sample2.tos = ToAddress(0x1900);
  sample2.stack[0] = ToAddress(0x1780);
  sample2.stack[1] = ToAddress(0x10000);  // non-existent.
  sample2.stack[2] = ToAddress(0x1620);
  sample2.frames_count = 3;
  generator.RecordTickSample(sample2);
  TickSample sample3;
  sample3.pc = ToAddress(0x1510);
  sample3.tos = ToAddress(0x1500);
  sample3.stack[0] = ToAddress(0x1910);
  sample3.stack[1] = ToAddress(0x1610);
  sample3.frames_count = 2;
  generator.RecordTickSample(sample3);

  CpuProfile* profile = profiles.StopProfiling("");
  CHECK(profile);
  ProfileTreeTestHelper top_down_test_helper(profile->top_down());
  CHECK(!top_down_test_helper.Walk(entry2));
  CHECK(!top_down_test_helper.Walk(entry3));
  ProfileNode* node1 = top_down_test_helper.Walk(entry1);
  CHECK(node1);
  CHECK_EQ(entry1, node1->entry());
  ProfileNode* node2 = top_down_test_helper.Walk(entry1, entry1);
  CHECK(node2);
  CHECK_EQ(entry1, node2->entry());
  ProfileNode* node3 = top_down_test_helper.Walk(entry1, entry2, entry3);
  CHECK(node3);
  CHECK_EQ(entry3, node3->entry());
  ProfileNode* node4 = top_down_test_helper.Walk(entry1, entry3, entry1);
  CHECK(node4);
  CHECK_EQ(entry1, node4->entry());

  delete entry1;
  delete entry2;
  delete entry3;
}


static void CheckNodeIds(ProfileNode* node, unsigned* expectedId) {
  CHECK_EQ((*expectedId)++, node->id());
  for (int i = 0; i < node->children()->length(); i++) {
    CheckNodeIds(node->children()->at(i), expectedId);
  }
}


TEST(SampleIds) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfilesCollection profiles(isolate);
  CpuProfiler profiler(isolate);
  profiles.set_cpu_profiler(&profiler);
  profiles.StartProfiling("", true);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = new CodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry* entry2 = new CodeEntry(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry* entry3 = new CodeEntry(i::Logger::FUNCTION_TAG, "ccc");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);
  generator.code_map()->AddCode(ToAddress(0x1700), entry2, 0x100);
  generator.code_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //                    -> aaa #3           - sample1
  // (root)#1 -> aaa #2 -> bbb #4 -> ccc #5 - sample2
  //                    -> ccc #6 -> aaa #7 - sample3
  TickSample sample1;
  sample1.timestamp = v8::base::TimeTicks::HighResolutionNow();
  sample1.pc = ToAddress(0x1600);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);
  TickSample sample2;
  sample2.timestamp = v8::base::TimeTicks::HighResolutionNow();
  sample2.pc = ToAddress(0x1925);
  sample2.stack[0] = ToAddress(0x1780);
  sample2.stack[1] = ToAddress(0x10000);  // non-existent.
  sample2.stack[2] = ToAddress(0x1620);
  sample2.frames_count = 3;
  generator.RecordTickSample(sample2);
  TickSample sample3;
  sample3.timestamp = v8::base::TimeTicks::HighResolutionNow();
  sample3.pc = ToAddress(0x1510);
  sample3.stack[0] = ToAddress(0x1910);
  sample3.stack[1] = ToAddress(0x1610);
  sample3.frames_count = 2;
  generator.RecordTickSample(sample3);

  CpuProfile* profile = profiles.StopProfiling("");
  unsigned nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(7u, nodeId - 1);

  CHECK_EQ(3, profile->samples_count());
  unsigned expected_id[] = {3, 5, 7};
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(expected_id[i], profile->sample(i)->id());
  }

  delete entry1;
  delete entry2;
  delete entry3;
}


TEST(NoSamples) {
  TestSetup test_setup;
  i::Isolate* isolate = CcTest::i_isolate();
  CpuProfilesCollection profiles(isolate);
  CpuProfiler profiler(isolate);
  profiles.set_cpu_profiler(&profiler);
  profiles.StartProfiling("", false);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = new CodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);

  // We are building the following calls tree:
  // (root)#1 -> aaa #2 -> aaa #3 - sample1
  TickSample sample1;
  sample1.pc = ToAddress(0x1600);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);

  CpuProfile* profile = profiles.StopProfiling("");
  unsigned nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(3u, nodeId - 1);

  CHECK_EQ(0, profile->samples_count());

  delete entry1;
}


static const ProfileNode* PickChild(const ProfileNode* parent,
                                    const char* name) {
  for (int i = 0; i < parent->children()->length(); ++i) {
    const ProfileNode* child = parent->children()->at(i);
    if (strcmp(child->entry()->name(), name) == 0) return child;
  }
  return NULL;
}


TEST(RecordStackTraceAtStartProfiling) {
  // This test does not pass with inlining enabled since inlined functions
  // don't appear in the stack trace.
  i::FLAG_turbo_inlining = false;

  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext(PROFILER_EXTENSION);
  v8::Context::Scope context_scope(env);
  std::unique_ptr<i::CpuProfiler> iprofiler(
      new i::CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  CompileRun(
      "function c() { startProfiling(); }\n"
      "function b() { c(); }\n"
      "function a() { b(); }\n"
      "a();\n"
      "stopProfiling();");
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  CpuProfile* profile = iprofiler->GetProfile(0);
  const ProfileTree* topDown = profile->top_down();
  const ProfileNode* current = topDown->root();
  const_cast<ProfileNode*>(current)->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     a
  //       b
  //         c
  // There can also be:
  //           startProfiling
  // if the sampler managed to get a tick.
  current = PickChild(current, "");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "a");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "b");
  CHECK(const_cast<ProfileNode*>(current));
  current = PickChild(current, "c");
  CHECK(const_cast<ProfileNode*>(current));
  CHECK(current->children()->length() == 0 ||
        current->children()->length() == 1);
  if (current->children()->length() == 1) {
    current = PickChild(current, "startProfiling");
    CHECK_EQ(0, current->children()->length());
  }
}


TEST(Issue51919) {
  CpuProfilesCollection collection(CcTest::i_isolate());
  CpuProfiler profiler(CcTest::i_isolate());
  collection.set_cpu_profiler(&profiler);
  i::EmbeddedVector<char*,
      CpuProfilesCollection::kMaxSimultaneousProfiles> titles;
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i) {
    i::Vector<char> title = i::Vector<char>::New(16);
    i::SNPrintF(title, "%d", i);
    CHECK(collection.StartProfiling(title.start(), false));
    titles[i] = title.start();
  }
  CHECK(!collection.StartProfiling("maximum", false));
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i)
    i::DeleteArray(titles[i]);
}


static const v8::CpuProfileNode* PickChild(const v8::CpuProfileNode* parent,
                                           const char* name) {
  for (int i = 0; i < parent->GetChildrenCount(); ++i) {
    const v8::CpuProfileNode* child = parent->GetChild(i);
    v8::String::Utf8Value function_name(child->GetFunctionName());
    if (strcmp(*function_name, name) == 0) return child;
  }
  return NULL;
}


TEST(ProfileNodeScriptId) {
  // This test does not pass with inlining enabled since inlined functions
  // don't appear in the stack trace.
  i::FLAG_turbo_inlining = false;

  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext(PROFILER_EXTENSION);
  v8::Context::Scope context_scope(env);
  std::unique_ptr<CpuProfiler> iprofiler(new CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  v8::Local<v8::Script> script_a =
      v8_compile(v8_str("function a() { startProfiling(); }\n"));
  script_a->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
      .ToLocalChecked();
  v8::Local<v8::Script> script_b =
      v8_compile(v8_str("function b() { a(); }\n"
                        "b();\n"
                        "stopProfiling();\n"));
  script_b->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
      .ToLocalChecked();
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  const v8::CpuProfile* profile = i::ProfilerExtension::last_profile;
  const v8::CpuProfileNode* current = profile->GetTopDownRoot();
  reinterpret_cast<ProfileNode*>(
      const_cast<v8::CpuProfileNode*>(current))->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     b
  //       a
  // There can also be:
  //         startProfiling
  // if the sampler managed to get a tick.
  current = PickChild(current, "");
  CHECK(const_cast<v8::CpuProfileNode*>(current));

  current = PickChild(current, "b");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK_EQ(script_b->GetUnboundScript()->GetId(), current->GetScriptId());

  current = PickChild(current, "a");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK_EQ(script_a->GetUnboundScript()->GetId(), current->GetScriptId());
}

static const char* line_number_test_source_existing_functions =
"function foo_at_the_first_line() {\n"
"}\n"
"foo_at_the_first_line();\n"
"function lazy_func_at_forth_line() {}\n";

static const char* line_number_test_source_profile_time_functions =
"// Empty first line\n"
"function bar_at_the_second_line() {\n"
"  foo_at_the_first_line();\n"
"}\n"
"bar_at_the_second_line();\n"
"function lazy_func_at_6th_line() {}";

int GetFunctionLineNumber(CpuProfiler& profiler, LocalContext& env,
                          const char* name) {
  CodeMap* code_map = profiler.generator()->code_map();
  i::Handle<i::JSFunction> func = i::Handle<i::JSFunction>::cast(
      v8::Utils::OpenHandle(*v8::Local<v8::Function>::Cast(
          env->Global()->Get(env.local(), v8_str(name)).ToLocalChecked())));
  CodeEntry* func_entry = code_map->FindEntry(func->abstract_code()->address());
  if (!func_entry)
    FATAL(name);
  return func_entry->line_number();
}

TEST(LineNumber) {
  CcTest::InitializeVM();
  LocalContext env;
  i::Isolate* isolate = CcTest::i_isolate();
  TestSetup test_setup;

  i::HandleScope scope(isolate);

  CompileRun(line_number_test_source_existing_functions);

  CpuProfiler profiler(isolate);
  profiler.StartProfiling("LineNumber");

  CompileRun(line_number_test_source_profile_time_functions);

  profiler.processor()->StopSynchronously();

  bool is_lazy = i::FLAG_lazy;
  CHECK_EQ(1, GetFunctionLineNumber(profiler, env, "foo_at_the_first_line"));
  CHECK_EQ(is_lazy ? 0 : 4,
           GetFunctionLineNumber(profiler, env, "lazy_func_at_forth_line"));
  CHECK_EQ(2, GetFunctionLineNumber(profiler, env, "bar_at_the_second_line"));
  CHECK_EQ(is_lazy ? 0 : 6,
           GetFunctionLineNumber(profiler, env, "lazy_func_at_6th_line"));

  profiler.StopProfiling("LineNumber");
}

TEST(BailoutReason) {
  i::FLAG_allow_natives_syntax = true;
  i::FLAG_always_opt = false;
  i::FLAG_opt = true;
  v8::HandleScope scope(CcTest::isolate());
  v8::Local<v8::Context> env = CcTest::NewContext(PROFILER_EXTENSION);
  v8::Context::Scope context_scope(env);
  std::unique_ptr<CpuProfiler> iprofiler(new CpuProfiler(CcTest::i_isolate()));
  i::ProfilerExtension::set_profiler(iprofiler.get());

  CHECK_EQ(0, iprofiler->GetProfilesCount());
  v8::Local<v8::Function> function = CompileRun(
                                         "function Debugger() {\n"
                                         "  startProfiling();\n"
                                         "}"
                                         "Debugger")
                                         .As<v8::Function>();
  i::Handle<i::JSFunction> i_function =
      i::Handle<i::JSFunction>::cast(v8::Utils::OpenHandle(*function));
  // Set a high deopt count to trigger bail out.
  i_function->shared()->set_opt_count(i::FLAG_max_deopt_count + 1);
  i_function->shared()->set_deopt_count(i::FLAG_max_deopt_count + 1);

  CompileRun(
      "%OptimizeFunctionOnNextCall(Debugger);"
      "Debugger();"
      "stopProfiling()");
  CHECK_EQ(1, iprofiler->GetProfilesCount());
  const v8::CpuProfile* profile = i::ProfilerExtension::last_profile;
  CHECK(profile);
  const v8::CpuProfileNode* current = profile->GetTopDownRoot();
  reinterpret_cast<ProfileNode*>(
      const_cast<v8::CpuProfileNode*>(current))->Print(0);
  // The tree should look like this:
  //  (root)
  //   ""
  //     kDeoptimizedTooManyTimes
  current = PickChild(current, "");
  CHECK(const_cast<v8::CpuProfileNode*>(current));

  current = PickChild(current, "Debugger");
  CHECK(const_cast<v8::CpuProfileNode*>(current));
  CHECK(!strcmp("Deoptimized too many times", current->GetBailoutReason()));
}
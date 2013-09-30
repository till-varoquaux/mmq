// -*- C++ -*-
/*
 * Copyright 2013 Humor rainbow Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @author Till Varoquaux <till@okcupid.com>
 */

#include "mmq.h"
#include <iostream>
#include <cassert>
extern "C" {
#include <unistd.h>
}

#define TEST_FILE "mmq_test_file"
int main() {
  mmaped_queue<uint32_t> v(TEST_FILE);
  assert(unlink(TEST_FILE) == 0);
  size_t exp_len = 0;
  for (size_t i = 1; i < 1000000000; i++) {
    assert(v.size() == exp_len);
    v.push(i);
    exp_len++;
    assert(v.front() == i + 1 - v.size());
    // if (i % 10000 == 359) {
    //   for (size_t j=0; j < v.size(); j++) {
    //     assert(v._to_rel_pos(v._to_abs_pos(j)) == j);
    //   }
    // }
    if (i % 10000000 == 0) {
      v.sync();
    }
    if (i % 3 != 0) {
      // std::cout << "{ ";
      // for (auto e : v) {
      //   std::cout << e << " ";
      // }
      // std::cout<< "}" << std::endl << std::endl;

      v.pop();
      exp_len--;
    }
  }
  v.sync();
  std::cout << "OK!" << std::endl;
  // v.sync();
  return 0;
}


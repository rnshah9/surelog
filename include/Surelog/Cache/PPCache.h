/*
 Copyright 2019 Alain Dargelas

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

/*
 * File:   PPCache.h
 * Author: alain
 *
 * Created on April 23, 2017, 8:49 PM
 */

#ifndef SURELOG_PPCACHE_H
#define SURELOG_PPCACHE_H
#pragma once

#include <Surelog/Cache/Cache.h>

#include <filesystem>

namespace SURELOG {

class PreprocessFile;

class PPCache : Cache {
 public:
  PPCache(PreprocessFile* pp);

  bool restore(bool errorsOnly);
  bool save();

 private:
  PPCache(const PPCache& orig) = delete;

  std::filesystem::path getCacheFileName_(
      const std::filesystem::path& fileName = "");
  bool restore_(const std::filesystem::path& cacheFileName, bool errorsOnly);
  bool restore_(const std::filesystem::path& cacheFileName,
                const std::unique_ptr<uint8_t[]>& buffer, bool errorsOnly);
  bool checkCacheIsValid_(const std::filesystem::path& cacheFileName);
  bool checkCacheIsValid_(const std::filesystem::path& cacheFileName,
                          const std::unique_ptr<uint8_t[]>& buffer);

  PreprocessFile* m_pp;
  bool m_isPrecompiled;
};

}  // namespace SURELOG

#endif /* SURELOG_PPCACHE_H */

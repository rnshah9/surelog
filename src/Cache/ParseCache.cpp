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
 * File:   ParseCache.cpp
 * Author: alain
 *
 * Created on April 29, 2017, 4:20 PM
 */

#include <Surelog/Cache/ParseCache.h>
#include <Surelog/Cache/parser_generated.h>
#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Design/Design.h>
#include <Surelog/Design/DesignElement.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/Library/Library.h>
#include <Surelog/Package/Precompiled.h>
#include <Surelog/SourceCompile/CompileSourceFile.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/ParseFile.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/FileUtils.h>

namespace SURELOG {
namespace fs = std::filesystem;

ParseCache::ParseCache(ParseFile* parser)
    : m_parse(parser), m_isPrecompiled(false) {}

static constexpr char FlbSchemaVersion[] = "1.2";

// TODO(hzeller): this should come from a function cacheFileResolver() or
// something that can be passed to the cache. That way, we can leave the
// somewhat hard-coded notion of where cache files are.
fs::path ParseCache::getCacheFileName_(const fs::path& svFileNameIn) {
  fs::path svFileName = svFileNameIn;
  CommandLineParser* clp =
      m_parse->getCompileSourceFile()->getCommandLineParser();
  Precompiled* prec = Precompiled::getSingleton();
  SymbolId cacheDirId = clp->getCacheDir();
  if (svFileName.empty()) svFileName = m_parse->getPpFileName();
  fs::path baseFileName = FileUtils::basename(svFileName);
  fs::path cacheFileName;
  if (prec->isFilePrecompiled(baseFileName)) {
    fs::path packageRepDir = m_parse->getSymbol(clp->getPrecompiledDir());
    cacheDirId =
        clp->mutableSymbolTable()->registerSymbol(packageRepDir.string());
    m_isPrecompiled = true;
    svFileName = baseFileName;
  } else {
    if (clp->noCacheHash()) {
      fs::path cacheDirName = m_parse->getSymbol(cacheDirId);
      const std::string& svFileTemp = svFileName.string();
      std::string svFile;
      int nbSlash = 0;
      // Bring back the .slpa file in the cache dir instead of alongside the
      // writepp source file
      for (char c : svFileTemp) {
        if (nbSlash >= 2) {
          svFile += c;
        }
        if (c == '/') {
          nbSlash++;
        }
      }
      cacheFileName = cacheDirName / (svFile + ".slpa");
    } else {
      svFileName = svFileName.parent_path().filename() / baseFileName;
    }
  }
  fs::path cacheDirName = m_parse->getSymbol(cacheDirId);
  Library* lib = m_parse->getLibrary();
  std::string libName = lib->getName();
  if (cacheFileName.empty()) {
    cacheFileName = cacheDirName / libName / (svFileName.string() + ".slpa");
  }

  FileUtils::mkDirs(cacheDirName / libName);
  return cacheFileName;
}

bool ParseCache::restore_(const fs::path& cacheFileName,
                          const std::unique_ptr<uint8_t[]>& buffer) {
  if (buffer == nullptr) return false;

  /* Restore Errors */
  const PARSECACHE::ParseCache* ppcache =
      PARSECACHE::GetParseCache(buffer.get());

  SymbolTable cacheSymbols;
  restoreErrors(ppcache->errors(), ppcache->symbols(), &cacheSymbols,
                m_parse->getCompileSourceFile()->getErrorContainer(),
                m_parse->getCompileSourceFile()->getSymbolTable());

  /* Restore design content (Verilog Design Elements) */
  FileContent* fileContent = m_parse->getFileContent();
  if (fileContent == nullptr) {
    fileContent =
        new FileContent(m_parse->getFileId(0), m_parse->getLibrary(),
                        m_parse->getCompileSourceFile()->getSymbolTable(),
                        m_parse->getCompileSourceFile()->getErrorContainer(),
                        nullptr, BadSymbolId);
    m_parse->setFileContent(fileContent);
    m_parse->getCompileSourceFile()->getCompiler()->getDesign()->addFileContent(
        m_parse->getFileId(0), fileContent);
  }
  for (const auto* elemc : *ppcache->elements()) {
    const std::string& elemName =
        cacheSymbols.getSymbol(SymbolId(elemc->name(), "<unknown>"));
    DesignElement* elem = new DesignElement(
        m_parse->getCompileSourceFile()->getSymbolTable()->registerSymbol(
            elemName),
        m_parse->getCompileSourceFile()->getSymbolTable()->registerSymbol(
            cacheSymbols.getSymbol(SymbolId(elemc->file_id(), "<unknown>"))),
        (DesignElement::ElemType)elemc->type(), NodeId(elemc->unique_id()),
        elemc->line(), elemc->column(), elemc->end_line(), elemc->end_column(),
        NodeId(elemc->parent()));
    elem->m_node = NodeId(elemc->node());
    elem->m_defaultNetType = (VObjectType)elemc->default_net_type();
    elem->m_timeInfo.m_type = (TimeInfo::Type)elemc->time_info()->type();
    elem->m_timeInfo.m_fileId =
        m_parse->getCompileSourceFile()->getSymbolTable()->registerSymbol(
            cacheSymbols.getSymbol(
                SymbolId(elemc->time_info()->file_id(), "<unknown>")));
    elem->m_timeInfo.m_line = elemc->time_info()->line();
    elem->m_timeInfo.m_timeUnit =
        (TimeInfo::Unit)elemc->time_info()->time_unit();
    elem->m_timeInfo.m_timeUnitValue = elemc->time_info()->time_unit_value();
    elem->m_timeInfo.m_timePrecision =
        (TimeInfo::Unit)elemc->time_info()->time_precision();
    elem->m_timeInfo.m_timePrecisionValue =
        elemc->time_info()->time_precision_value();
    const std::string& fullName =
        fileContent->getLibrary()->getName() + "@" + elemName;
    fileContent->addDesignElement(fullName, elem);
  }

  /* Restore design objects */
  auto objects = ppcache->objects();
  restoreVObjects(objects, cacheSymbols,
                  m_parse->getCompileSourceFile()->getSymbolTable(),
                  m_parse->getFileId(0), fileContent);

  return true;
}

bool ParseCache::checkCacheIsValid_(const fs::path& cacheFileName,
                                    const std::unique_ptr<uint8_t[]>& buffer) {
  if (buffer == nullptr) return false;

  CommandLineParser* clp =
      m_parse->getCompileSourceFile()->getCommandLineParser();

  if (!PARSECACHE::ParseCacheBufferHasIdentifier(buffer.get())) {
    return false;
  }

  if (clp->noCacheHash()) {
    return true;
  }

  const PARSECACHE::ParseCache* ppcache =
      PARSECACHE::GetParseCache(buffer.get());
  auto header = ppcache->header();
  if (!m_isPrecompiled &&
      !checkIfCacheIsValid(header, FlbSchemaVersion, cacheFileName)) {
    return false;
  }

  return true;
}

bool ParseCache::isValid() {
  fs::path cacheFileName = getCacheFileName_();
  return checkCacheIsValid_(cacheFileName, openFlatBuffers(cacheFileName));
}

bool ParseCache::restore() {
  CommandLineParser* clp =
      m_parse->getCompileSourceFile()->getCommandLineParser();
  bool cacheAllowed = clp->cacheAllowed();
  if (!cacheAllowed) return false;

  fs::path cacheFileName = getCacheFileName_();
  auto buffer = openFlatBuffers(cacheFileName);
  if (!checkCacheIsValid_(cacheFileName, buffer)) {
    // char path [10000];
    // char* p = getcwd(path, 9999);
    // if (!clp->parseOnly())
    //   std::cout << "Cache miss for: " << cacheFileName << " pwd: " << p <<
    //   "\n";
    return false;
  }

  return restore_(cacheFileName, buffer);
}

bool ParseCache::save() {
  CommandLineParser* clp =
      m_parse->getCompileSourceFile()->getCommandLineParser();
  bool cacheAllowed = clp->cacheAllowed();
  bool parseOnly = clp->parseOnly();

  if (!cacheAllowed) return true;
  FileContent* fcontent = m_parse->getFileContent();
  if (fcontent) {
    if (fcontent->getVObjects().size() > Cache::Capacity) {
      clp->setCacheAllowed(false);
      Location loc(BadSymbolId);
      Error err(ErrorDefinition::CMD_CACHE_CAPACITY_EXCEEDED, loc);
      m_parse->getCompileSourceFile()->getErrorContainer()->addError(err);
      return false;
    }
  }
  fs::path svFileName = m_parse->getPpFileName();
  fs::path origFileName = svFileName;
  if (parseOnly) {
    SymbolId cacheDirId = clp->getCacheDir();
    fs::path cacheDirName = m_parse->getSymbol(cacheDirId);
    origFileName = cacheDirName / ".." / origFileName;
  }
  fs::path cacheFileName = getCacheFileName_();
  if (strstr(cacheFileName.string().c_str(), "@@BAD_SYMBOL@@")) {
    // Any fake(virtual) file like builtin.sv
    return true;
  }
  flatbuffers::FlatBufferBuilder builder(1024);
  /* Create header section */
  auto header = createHeader(builder, FlbSchemaVersion, origFileName);

  /* Cache the errors and canonical symbols */
  ErrorContainer* errorContainer =
      m_parse->getCompileSourceFile()->getErrorContainer();
  fs::path subjectFile = m_parse->getFileName(LINE1);
  SymbolId subjectFileId =
      m_parse->getCompileSourceFile()->getSymbolTable()->registerSymbol(
          subjectFile.string());
  SymbolTable cacheSymbols;
  auto errorCache = cacheErrors(
      builder, &cacheSymbols, errorContainer,
      *m_parse->getCompileSourceFile()->getSymbolTable(), subjectFileId);

  /* Cache the design content */
  std::vector<flatbuffers::Offset<PARSECACHE::DesignElement>> element_vec;
  if (fcontent)
    for (const DesignElement* elem : fcontent->getDesignElements()) {
      const TimeInfo& info = elem->m_timeInfo;
      const std::string& elemName =
          m_parse->getCompileSourceFile()->getSymbolTable()->getSymbol(
              elem->m_name);
      auto timeInfo = CACHE::CreateTimeInfo(
          builder, static_cast<uint16_t>(info.m_type),
          (RawSymbolId)cacheSymbols.registerSymbol(
              m_parse->getCompileSourceFile()->getSymbolTable()->getSymbol(
                  info.m_fileId)),
          info.m_line, static_cast<uint16_t>(info.m_timeUnit),
          info.m_timeUnitValue, static_cast<uint16_t>(info.m_timePrecision),
          info.m_timePrecisionValue);
      element_vec.push_back(PARSECACHE::CreateDesignElement(
          builder, (RawSymbolId)cacheSymbols.registerSymbol(elemName),
          (RawSymbolId)cacheSymbols.registerSymbol(
              m_parse->getCompileSourceFile()->getSymbolTable()->getSymbol(
                  elem->m_fileId)),
          elem->m_type, (RawNodeId)elem->m_uniqueId, elem->m_line,
          elem->m_column, elem->m_endLine, elem->m_endColumn, timeInfo,
          (RawNodeId)elem->m_parent, (RawNodeId)elem->m_node,
          elem->m_defaultNetType));
    }
  auto elementList = builder.CreateVector(element_vec);

  /* Cache the design objects */
  std::vector<CACHE::VObject> object_vec =
      cacheVObjects(fcontent, &cacheSymbols,
                    *m_parse->getCompileSourceFile()->getSymbolTable(),
                    m_parse->getFileId(0));
  auto objectList = builder.CreateVectorOfStructs(object_vec);

  auto symbolVec = createSymbolCache(builder, cacheSymbols);
  /* Create Flatbuffers */
  auto ppcache = PARSECACHE::CreateParseCache(
      builder, header, errorCache, symbolVec, elementList, objectList);
  FinishParseCacheBuffer(builder, ppcache);

  /* Save Flatbuffer */
  bool status = saveFlatbuffers(builder, cacheFileName);

  return status;
}
}  // namespace SURELOG

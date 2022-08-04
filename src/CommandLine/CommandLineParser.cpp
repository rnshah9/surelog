/*
 Copyright 2017-2022 Alain Dargelas

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
 * File:   CommandLineParser.cpp
 * Author: alain
 *
 * Created on January 26, 2017, 9:47 PM
 */

#include <Surelog/API/PythonAPI.h>
#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/FileUtils.h>
#include <Surelog/Utils/StringUtils.h>

#if defined(_MSC_VER)
#include <direct.h>
#define PATH_MAX _MAX_PATH
#else
#include <sys/param.h>
#include <unistd.h>
#endif

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace SURELOG {
namespace fs = std::filesystem;

static constexpr std::string_view defaultLogFileName = "surelog.log";
static constexpr std::string_view defaultCacheDirName = "cache";
static constexpr std::string_view defaultCompileUnitDirName = "slpp_unit";
static constexpr std::string_view defaultCompileAllDirName = "slpp_all";
static constexpr std::string_view defaultPrecompiledDirName = "pkg";

// !!! Update this number when the grammar changes !!!
//         Or when the cache schema changes
//        This will render the cache invalid
std::string_view CommandLineParser::getVersionNumber() {
  static constexpr std::string_view kVersionNumber("1.35");
  return kVersionNumber;
}

static const std::initializer_list<std::string_view> copyright = {
    "Copyright (c) 2017-2022 Alain Dargelas,",
    "http://www.apache.org/licenses/LICENSE-2.0"};

static const std::initializer_list<std::string_view> banner = {
    "********************************************",
    "*  SURELOG SystemVerilog  Compiler/Linter  *",
    "********************************************",
};

static const std::initializer_list<std::string_view> footer = {
    "********************************************",
    "*   End SURELOG SVerilog Compiler/Linter   *",
    "********************************************",
};

static const std::initializer_list<std::string_view> helpText = {
    "  ------------ SURELOG HELP --------------",
    "",
    "STANDARD VERILOG COMMAND LINE:",
    "  -f <file>             Accepts a file containing command line arguments",
    "  -v <file>             Library file",
    "  -y <path>             Library directory",
    "  +incdir+<dir>[+<dir>...] Specifies include paths",
    "  -Idir                 Specifies include paths",
    "  +libext+<extname>+... Specifies the library extensions, "  // NOLINT
    "default is .v+.sv",
    "  <file>.v              Verilog File",
    "  <file>.sv             SystemVerilog File",
    "  +liborder             Lib Order option (ignored)",
    "  +librescan            Lib Rescan option (ignored)",
    "  +libverbose           Lib Verbose option (ignored)",
    "  +nolibcell            No Lib Cell option (ignored)",
    "  +define+<name>=<value>[+<name>=<value>...]",
    "                        Defines a macro and optionally its value",
    "  -L <libName>          Defines library compilation order",
    "  -map <mapFile>        Specifies a library mapping file (multiple -map "
    "options supported)",
    "  -cfgfile <confiFile>  Specifies a configuration file (multiple -cfgFile "
    "options supported)",
    "  -cfg <configName>     Specifies a configuration to use (multiple -cfg "
    "options supported)",
    "  -Dvar=value           Same as env var definition for -f files var "
    "substitution",
    "  -Pparameter=value     Top level parameter override",
    "  -pvalue+parameter=value Top level parameter override",
    "  -sverilog/-sv         Forces all files to be parsed as SystemVerilog "
    "files",
    "  -sv <file>            Forces the following file to be parsed as "
    "SystemVerilog file",
    "FLOWS OPTIONS:",
    "  -fileunit             Compiles each Verilog file as an independent",
    "                        compilation unit (under slpp_unit/ if -writepp "
    "used)",
    "  -diffcompunit         Compiles both all files as a whole unit and",
    "                        separate compilation units to perform diffs",
    "  -parse                Parse/Compile/Elaborate the files after "
    "pre-processing step",
    "  -noparse              Turns off Parsing & Compilation & Elaboration",
    "  -nocomp               Turns off Compilation & Elaboration",
    "  -noelab               Turns off Elaboration",
    "  -parseonly            Only Parses, reloads Preprocessor saved db",
    "  -init                 Initialize cache for separate compile flow "
    "(-sepcomp, -link)",
    "  -sepcomp              Separate compilation, each invocation creates a "
    "compilation unit",
    "  -link                 Link and elaborate the separately compiled files",
    "  -elabuhdm             Forces UHDM/VPI Full Elaboration, default is the "
    "Folded Model",
    "  -nouhdm               No UHDM db write",
    "  -top/--top-module <module> Top level module for elaboration (multiple "
    "cmds ok)",
    "  -bb_mod <module>      Blackbox module (multiple cmds ok, ex: -bb_mod "
    "work@top)",
    "  -bb_inst <instance>   Blackbox instance (multiple cmds ok, ex: -bb_inst "
    "work@top.u1)",
    "  -batch <batch.txt>    Runs all the tests specified in the file in batch "
    "mode",
    "                        Tests are expressed as one full command line per "
    "line.",
    "  --enable-feature=<feature>",
    "  --disable-feature=<feature>",
    "    Features: parametersubstitution Enables substitution of assignment "
    "patterns in parameters",
    "              letexprsubstitution Enables Let expr substitution "
    "(Inlining)",
#ifdef SURELOG_WITH_PYTHON
    "  -pythonlistener       Enables the Parser Python Listener",
    "  -pythonlistenerfile <script.py> Specifies the AST python listener file",
    "  -pythonevalscriptperfile <script.py>  Eval the Python script on each "
    "source file (Multithreaded)",
    "  -pythonevalscript <script.py> Eval the Python script at the design "
    "level",
    "  -nopython             Turns off all Python features, including waivers",
    "  -withpython           Turns on all Python features, including waivers",
    "  -strictpythoncheck    Turns on strict Python checks",
#endif
    "  -mt/--threads <nb_max_threads> 0 up to 512 max threads, 0 or 1 being "
    "single "
    "threaded,",
    "                        if \"max\" is given, the program will use one ",
    "                        thread per core on the host",
    "  -mp <mb_max_process>  0 up to 512 max processes, 0 or 1 being single "
    "process",
    "  -lowmem               Minimizes memory high water mark (uses multiple "
    "staggered processes for preproc, parsing and elaboration)",
    "  -split <line number>  Split files or modules larger than specified line "
    "number for multi thread compilation",
    "  -timescale=<timescale> Specifies the overall timescale",
    "  -nobuiltin            Do not parse SV builtin classes (array...)",
    "",
    "TRACES OPTIONS:",
    "  -d <int>              Debug <level> 1-4, lib, ast, inst, incl, uhdm, "
    "cache, "
    "coveruhdm, vpi_ids",
    "  -nostdout             Mutes Standard output",
    "  -verbose              Gives verbose processing information",
    "  -profile              Gives Profiling information",
    "  -replay               Enables replay of internal elaboration errors",
    "  -l <file>             Specifies log file, default is surelog.log under "
    "output dir",
    "",
    "OUTPUT OPTIONS:",
    "  -odir/--Mdir <dir>    Specifies the output directory, default is ./",
    "  -writeppfile <file>   Writes out Preprocessor output in file",
    "                        (all compilation units will override this file)",
    "  -writepp              Writes out Preprocessor output (all compilation",
    "                        units will generate files under slpp_all/ or "
    "slpp_unit/)",
    "  -lineoffsetascomments Writes the preprocessor line offsets as comments "
    "as opposed as parser directives",
    "  -nocache              Default allows to create a cache for include "
    "files, this option prevents it",
    "  -cache <dir>          Specifies the cache directory, default is "
    "slpp_all/cache or slpp_unit/cache",
    "  -nohash               Don't use hash mechanism for cache file path, "
    "always treat cache as valid (no timestamp/dependancy check)",
    "  -createcache          Create cache for precompiled packages",
    "  -filterdirectives     Filters out simple directives like",
    "                        `default_nettype in pre-processor's output",
    "  -filterprotected      Filters out protected regions in pre-processor's "
    "output",
    "  -filtercomments       Filters out comments in pre-processor's output",
    "  -outputlineinfo       Outputs SLline directives in pre-processor's "
    "output",
    "  -pploc                Output message location in terms of post "
    "preprocessor location",
    "  -noinfo               Filters out INFO messages",
    "  -nonote               Filters out NOTE messages",
    "  -nowarning            Filters out WARNING messages",
    "  -synth                Reports non-synthesizable constructs",
    "                        Honnors //pragma translate_off  ,  //pragma "
    "translate_on",
    "  -o <path>             Turns on all compilation stages, produces all",
    "  -builtin <path>       Alternative path to python/ and pkg/ dirs",
    "outputs under that path",
    "  -cd <dir>             Internally change directory to <dir>",
    "  -exe <command>        Post execute a system call <command>, passes it "
    "the ",
    "                        preprocessor file list.",
    "  --help                This help",
    "  --version             Surelog version",
    "RETURN CODE:",
    "   Bit mask the return code, more than 1 bit can be on.",
    "   0   - No issues",
    "   0x1 - Fatal error(s)",
    "   0x2 - Syntax error(s)",
    "   0x4 - Error(s)"};

static bool is_number(const std::string_view s) {
  return s.find_first_not_of("-.0123456789") == std::string_view::npos;
}

static bool is_c_file(const std::string_view s) {
  const std::string_view ext = StringUtils::leaf(s);
  return (ext == "c" || ext == "cpp" || ext == "cc");
}

static std::string printStringArray(
    const std::initializer_list<std::string_view>& all_strings) {
  std::string report;
  for (std::string_view s : all_strings) {
    report.append(s).append("\n");
  }
  report += "\n";
  return report;
}

void CommandLineParser::withPython() {
#ifdef SURELOG_WITH_PYTHON
  m_pythonAllowed = true;
#endif
}

std::string CommandLineParser::currentDateTime() {
  time_t now = time(0);
  struct tm tstruct = *localtime(&now);
  // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
  // for more information about date/time format
  char buf[80] = {'\0'};
  strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
  return buf;
}

static std::string BuildIdentifier() {
  return StrCat("VERSION: ", CommandLineParser::getVersionNumber(),
                "\nBUILT  : ", __DATE__, "\n");
}

void CommandLineParser::logBanner(int argc, const char** argv) {
  std::string banners = printStringArray(banner);
  std::string copyrights = printStringArray(copyright);
  m_errors->printToLogFile(banners);
  m_errors->printToLogFile(copyrights);
  const std::string version = BuildIdentifier();
  const std::string date = "DATE   : " + currentDateTime() + "\n";
  std::string cmd = "COMMAND:";
  for (int i = 1; i < argc; i++) {
    cmd += std::string(" ") + argv[i];
  }
  cmd += "\n\n";
  m_errors->printToLogFile(version);
  m_errors->printToLogFile(date);
  m_errors->printToLogFile(cmd);
}

void CommandLineParser::logFooter() {
  std::string footers = "\n";
  footers += printStringArray(footer);
  m_errors->printToLogFile(footers);
}

CommandLineParser::CommandLineParser(ErrorContainer* errors,
                                     SymbolTable* symbolTable,
                                     bool diff_comp_mode, bool fileUnit)
    : m_writePpOutputFileId(BadSymbolId),
      m_writePpOutput(false),
      m_filterFileLine(true),
      m_debugLevel(0),
      m_errors(errors),
      m_symbolTable(symbolTable),
      m_lineOffsetsAsComments(false),
      m_liborder(false),
      m_librescan(false),
      m_libverbose(false),
      m_nolibcell(false),
      m_muteStdout(false),
      m_verbose(false),
      m_fileunit(fileUnit),
      m_filterSimpleDirectives(false),
      m_filterProtectedRegions(false),
      m_filterComments(false),
      m_parse(false),
      m_parseOnly(false),
      m_compile(false),
      m_elaborate(false),
      m_parametersubstitution(true),
      m_letexprsubstitution(true),
      m_diff_comp_mode(diff_comp_mode),
      m_help(false),
      m_cacheAllowed(true),
      m_debugCache(false),
      m_nbMaxTreads(0),
      m_nbMaxProcesses(0),
      m_fullCompileDir(BadSymbolId),
      m_cacheDirId(BadSymbolId),
      m_note(true),
      m_info(true),
      m_warning(true),
      m_pythonListener(false),
      m_debugAstModel(false),
      m_debugInstanceTree(false),
      m_debugLibraryDef(false),
      m_useTbb(false),
#ifdef SURELOG_WITH_PYTHON
      m_pythonAllowed(true),
#else
      m_pythonAllowed(false),
#endif
      m_nbLinesForFileSplitting(10000000),
      m_pythonEvalScriptPerFile(false),
      m_pythonEvalScript(false),
      m_pythonEvalScriptPerFileId(BadSymbolId),
      m_pythonEvalScriptId(BadSymbolId),
      m_pythonListenerFileId(BadSymbolId),
      m_debugIncludeFileInfo(false),
      m_createCache(false),
      m_profile(false),
      m_parseBuiltIn(true),
      m_ppOutputFileLocation(false),
      m_sverilog(false),
      m_dumpUhdm(false),
      m_elabUhdm(false),
      m_coverUhdm(false),
      m_showVpiIDs(false),
      m_replay(false),
      m_uhdmStats(false),
      m_lowMem(false),
      m_writeUhdm(true),
      m_nonSynthesizable(false),
      m_noCacheHash(false),
      m_sepComp(false),
      m_link(false) {
  m_errors->registerCmdLine(this);
  m_logFileId = m_symbolTable->registerSymbol(defaultLogFileName);
  m_compileUnitDirectory =
      m_symbolTable->registerSymbol(defaultCompileUnitDirName);
  m_compileAllDirectory =
      m_symbolTable->registerSymbol(defaultCompileAllDirName);
  m_outputDir = m_symbolTable->registerSymbol(".");
  m_defaultLogFileId = m_symbolTable->registerSymbol(defaultLogFileName);
  m_defaultCacheDirId = m_symbolTable->registerSymbol(defaultCacheDirName);
  m_precompiledDirId = m_symbolTable->registerSymbol(defaultPrecompiledDirName);
  if (m_diff_comp_mode) {
    m_muteStdout = true;
    m_verbose = false;
  }
  m_libraryExtensions.push_back(
      m_symbolTable->registerSymbol(".v"));  // default
}

// Undecorate command line arg by removing any space, single-quotes,
// and/or double-quotes at the front or at the back
static std::string_view undecorateArg(std::string_view arg) {
  // Strip out any space character at front and back
  while (!arg.empty() && std::isspace(arg.front())) arg.remove_prefix(1);
  while (!arg.empty() && std::isspace(arg.back())) arg.remove_suffix(1);

  // Remove any surrounding quotes
  if ((arg.size() > 1) && (((arg.front() == '\"') && (arg.back() == '\"')) ||
                           ((arg.front() == '\'') && (arg.back() == '\'')))) {
    arg.remove_prefix(1);
    arg.remove_suffix(1);

    // Strip any space once again, post removal of quotes
    while (!arg.empty() && std::isspace(arg.front())) arg.remove_prefix(1);
    while (!arg.empty() && std::isspace(arg.back())) arg.remove_suffix(1);
  }

  return arg;
}

void CommandLineParser::splitPlusArg_(const std::string& s,
                                      const std::string& prefix,
                                      std::vector<SymbolId>& container) {
  std::istringstream f(s);
  std::string tmp;
  while (getline(f, tmp, '+')) {
    if (!tmp.empty() && (tmp != prefix)) {
      SymbolId id = m_symbolTable->registerSymbol(tmp);
      container.push_back(id);
    }
  }
}

void CommandLineParser::splitPlusArg_(
    const std::string& s, const std::string& prefix,
    std::map<SymbolId, std::string, SymbolIdLessThanComparer>& container) {
  std::istringstream f(s);
  std::string tmp;
  while (getline(f, tmp, '+')) {
    if (!tmp.empty() && (tmp != prefix)) {
      std::string def;
      std::string value;
      const size_t loc = tmp.find('=');
      if (loc == std::string::npos) {
        def = tmp;
      } else {
        def = tmp.substr(0, loc);
        value = tmp.substr(loc + 1);
      }
      if (!def.empty()) {
        SymbolId id = m_symbolTable->registerSymbol(def);
        container.emplace(id, value);
      }
    }
  }
}

/* Custom parser for +arguments */
bool CommandLineParser::plus_arguments_(const std::string& s) {
  constexpr std::string_view incdir("+incdir+");
  constexpr std::string_view libext("+libext+");
  constexpr std::string_view define("+define+");
  if (s.empty()) return false;
  if (s.at(0) != '+') return false;
  if (s.compare(0, incdir.size(), incdir) == 0) {
    splitPlusArg_(s, "incdir", m_includePaths);
    return true;
  }
  if (s.compare(0, libext.size(), libext) == 0) {
    m_libraryExtensions.clear();
    splitPlusArg_(s, "libext", m_libraryExtensions);
    return true;
  }
  if (s.compare(0, define.size(), define) == 0) {
    splitPlusArg_(s, "define", m_defineList);
    return true;
  }
  return false;
}

void CommandLineParser::processArgs_(const std::vector<std::string>& args,
                                     std::vector<std::string>& container) {
  for (unsigned int i = 0; i < args.size(); i++) {
    std::string arg(undecorateArg(args[i]));
    if (arg == "-f") {
      std::string f(undecorateArg(args[++i]));
      std::ifstream ifs(f);
      if (!ifs) {
        Location loc(m_symbolTable->registerSymbol(f));
        Error err(ErrorDefinition::CMD_DASH_F_FILE_DOES_NOT_EXIST, loc);
        m_errors->addError(err);
      } else {
        std::stringstream ss;
        ss << ifs.rdbuf();
        ifs.close();
        std::string fileContent = ss.str();
        fileContent = StringUtils::removeComments(fileContent);
        fileContent = StringUtils::evaluateEnvVars(fileContent);
        std::vector<std::string> argsInFile;
        StringUtils::tokenize(fileContent, " \n\t\r", argsInFile);
        processArgs_(argsInFile, container);
      }
    } else if (arg == "-link") {
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
      m_writePpOutput = true;
      m_link = true;
      fs::path odir = m_symbolTable->getSymbol(m_outputDir);
      odir /= m_symbolTable->getSymbol(
          (fileunit() ? m_compileUnitDirectory : m_compileAllDirectory));
      odir = FileUtils::getPreferredPath(odir);
      if (FileUtils::fileExists(odir)) {
        for (const auto& entry : fs::directory_iterator(odir)) {
          const fs::path& flist = entry.path();
          if (flist.extension() == ".sep_lst") {
            std::string f(undecorateArg(flist.string()));
            std::ifstream ifs(f);
            if (!ifs) {
              Location loc(m_symbolTable->registerSymbol(f));
              Error err(ErrorDefinition::CMD_DASH_F_FILE_DOES_NOT_EXIST, loc);
              m_errors->addError(err);
            } else {
              std::stringstream ss;
              ss << ifs.rdbuf();
              ifs.close();
              std::string fileContent = ss.str();
              fileContent = StringUtils::removeComments(fileContent);
              fileContent = StringUtils::evaluateEnvVars(fileContent);
              std::vector<std::string> argsInFile;
              StringUtils::tokenize(fileContent, " \n\t\r", argsInFile);
              processArgs_(argsInFile, container);
            }
          }
        }
      }
    } else if (!arg.empty()) {
      container.emplace_back(arg);
    }
  }
}

void CommandLineParser::processOutputDirectory_(
    const std::vector<std::string>& args) {
  for (unsigned int i = 0; i < args.size(); i++) {
    std::string arg(undecorateArg(args[i]));
    if (arg == "-odir" || arg == "-o" || arg == "--Mdir") {
      if (i == args.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(args[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_ODIR, loc);
        m_errors->addError(err);
        break;
      }
      fs::path path = FileUtils::getPreferredPath(undecorateArg(args[++i]));
      m_outputDir = m_symbolTable->registerSymbol(path.string());
    }
  }
}

// Try to find the full absolute path of the program currently running.
static fs::path GetProgramNameAbsolutePath(const char* progname) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
  const char PATH_DELIMITER = ';';
#else
  char buf[PATH_MAX];
  // If the executable is invoked with a path, we can extract it from there,
  // otherwise, we use some operating system trick to find that path:
  // In Linux, the current running binary is symbolically linked from
  // /proc/self/exe which we can resolve.
  // It won't resolve anything on other platforms, but doesnt harm either.
  for (const char* testpath : {progname, "/proc/self/exe"}) {
    const char* const program_name = realpath(testpath, buf);
    if (program_name != nullptr) return program_name;
  }
  const char PATH_DELIMITER = ':';
#endif

  // Still not found, let's go through the $PATH and see what comes up first.
  const char* const path = std::getenv("PATH");
  if (path != nullptr) {
    std::stringstream search_path(path);
    std::string path_element;
    fs::path program_path;
    while (std::getline(search_path, path_element, PATH_DELIMITER)) {
      const fs::path testpath = path_element / fs::path(progname);
      if (FileUtils::getFullPath(testpath, &program_path)) {
        return program_path;
      }
    }
  }

  return progname;  // Didn't find anything, return progname as-is.
}

bool CommandLineParser::parseCommandLine(int argc, const char** argv) {
  m_exePath = GetProgramNameAbsolutePath(argv[0]);
  const fs::path exe_dir = FileUtils::getPathName(m_exePath);
  const std::vector<fs::path> search_path = {
      exe_dir, exe_dir / ".." / "lib" / "surelog"};

  m_precompiledDirId = m_symbolTable->registerSymbol(
      FileUtils::getPreferredPath(exe_dir / "pkg").string());
  for (const fs::path& dir : search_path) {
    const fs::path pkg_dir = FileUtils::getPreferredPath(dir / "pkg");
    if (FileUtils::fileIsDirectory(pkg_dir)) {
      m_precompiledDirId = m_symbolTable->registerSymbol(pkg_dir.string());
      break;
    }
  }

  std::vector<std::string> cmd_line;
  for (int i = 1; i < argc; i++) {
    cmd_line.emplace_back(undecorateArg(argv[i]));
    const std::string& arg = cmd_line.back();

    if (arg == "-help" || arg == "-h" || arg == "--help") {
      m_help = true;
      std::string help = printStringArray(helpText);
      m_errors->init();
      logBanner(argc, argv);
      std::cout << help;
      return true;
    }
    if (arg == "--version") {
      std::cout << BuildIdentifier() << std::flush;
      m_help = true;
      return true;
    } else if (arg == "-cd") {
      if (i < argc - 1) {
        std::string newDir(undecorateArg(argv[i + 1]));
        int ret = chdir(newDir.c_str());
        if (ret < 0) {
          std::cerr << "Could not change directory to " << newDir << std::endl;
        }
      }
    } else if (arg == "-builtin") {
      if (i < argc - 1) {
        m_builtinPath = undecorateArg(argv[i + 1]);
      }
    } else if (arg == "-l") {
      if (i < argc - 1) {
        m_logFileId = m_symbolTable->registerSymbol(undecorateArg(argv[i + 1]));
      }
    } else if (arg.find("-D") == 0) {
      std::string def;
      std::string value;
      const size_t loc = arg.find('=');
      if (loc == std::string::npos) {
        def = arg.substr(2);
      } else {
        def = arg.substr(2, loc - 2);
        value = arg.substr(loc + 1);
      }
      if (!def.empty()) {
        StringUtils::registerEnvVar(def, value);
        SymbolId id = m_symbolTable->registerSymbol(def);
        m_defineList.emplace(id, value);
      }
    }
  }

  std::vector<std::string> all_arguments;
  processOutputDirectory_(cmd_line);
  processArgs_(cmd_line, all_arguments);

  /*
  std::string cmd = "EXPANDED CMD:";
  for (unsigned int i = 0; i < all_arguments.size(); i++) {
    cmd += std::string(" ") + all_arguments[i];
  }
  cmd += "\n\n";
  std::cout << cmd;
  */

  for (const auto& argument : all_arguments) {
    if (argument == "-nobuiltin") {
      m_parseBuiltIn = false;
    } else if (argument == "-fileunit") {
      if (m_diff_comp_mode == false)  // Controlled by constructor
        m_fileunit = true;
    } else if (argument == "-mutestdout") {
      m_muteStdout = true;
    } else if (argument == "-nopython") {
      m_pythonAllowed = false;
    } else if (argument == "-withpython") {
      withPython();
    }
  }
  bool status = prepareCompilation_(argc, argv);
  if (!status) return status;

  for (unsigned int i = 0; i < all_arguments.size(); i++) {
    if (all_arguments[i].empty() || plus_arguments_(all_arguments[i])) {
      // handled by plus_arguments
    } else if (all_arguments[i] == "-d") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_DEBUG_MISSING_LEVEL, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      if (all_arguments[i] == "ast") {
        m_debugAstModel = true;
      } else if (all_arguments[i] == "inst") {
        m_debugInstanceTree = true;
      } else if (all_arguments[i] == "lib") {
        m_debugLibraryDef = true;
      } else if (all_arguments[i] == "incl") {
        m_debugIncludeFileInfo = true;
      } else if (all_arguments[i] == "uhdm") {
        m_dumpUhdm = true;
      } else if (all_arguments[i] == "uhdmstats") {
        m_uhdmStats = true;
      } else if (all_arguments[i] == "coveruhdm") {
        m_coverUhdm = true;
      } else if (all_arguments[i] == "cache") {
        m_debugCache = true;
      } else if (all_arguments[i] == "vpi_ids") {
        m_showVpiIDs = true;
      } else if (all_arguments[i] == "coverelab") {
        // Ignored!
      } else if (is_number(all_arguments[i])) {
        int debugLevel = std::stoi(all_arguments[i]);
        if (debugLevel < 0 || debugLevel > 4) {
          Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
          Error err(ErrorDefinition::CMD_DEBUG_INCORRECT_LEVEL, loc);
          m_errors->addError(err);
        } else {
          m_debugLevel = debugLevel;
        }
      } else {
        std::cerr << "Option: " << all_arguments[i] << " ignored." << std::endl;
      }
    } else if (all_arguments[i].find("--enable-feature=") == 0) {
      std::string features = all_arguments[i].substr(17);
      std::istringstream f(features);
      std::string tmp;
      while (getline(f, tmp, ',')) {
        if (tmp == "parametersubstitution") {
          m_parametersubstitution = true;
        } else if (tmp == "letexprsubstitution") {
          m_letexprsubstitution = true;
        } else {
          std::cerr << "Feature: " << tmp << " ignored." << std::endl;
        }
      }
    } else if (all_arguments[i].find("--disable-feature=") == 0) {
      std::string features = all_arguments[i].substr(18);
      std::istringstream f(features);
      std::string tmp;
      while (getline(f, tmp, ',')) {
        if (tmp == "parametersubstitution") {
          m_parametersubstitution = false;
        } else if (tmp == "letexprsubstitution") {
          m_letexprsubstitution = false;
        } else {
          std::cerr << "Feature: " << tmp << " ignored." << std::endl;
        }
      }
    } else if (all_arguments[i].find("-timescale=") == 0) {
      std::string timescale = all_arguments[i].substr(11);
      if (timescale.empty()) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_TIMESCALE_MISSING_SETTING, loc);
        m_errors->addError(err);
        break;
      }
      m_timescale = timescale;
    } else if (all_arguments[i].find("-D") == 0) {
      std::string def;
      std::string value;
      const size_t loc = all_arguments[i].find('=');
      if (loc == std::string::npos) {
        def = all_arguments[i].substr(2);
      } else {
        def = all_arguments[i].substr(2, loc - 2);
        value = all_arguments[i].substr(loc + 1);
      }
      if (!def.empty()) {
        StringUtils::registerEnvVar(def, value);
        SymbolId id = m_symbolTable->registerSymbol(def);
        m_defineList.emplace(id, value);
      }
    } else if (all_arguments[i].find("-P") == 0) {
      std::string def;
      std::string value;
      const size_t loc = all_arguments[i].find('=');
      if (loc == std::string::npos) {
        def = all_arguments[i].substr(2);
      } else {
        def = all_arguments[i].substr(2, loc - 2);
        value = all_arguments[i].substr(loc + 1);
      }
      if (!def.empty()) {
        SymbolId id = m_symbolTable->registerSymbol(def);
        m_paramList.emplace(id, value);
      }
    } else if (all_arguments[i].find("-pvalue+") == 0) {
      std::string def;
      std::string value;
      const size_t loc = all_arguments[i].find('=');
      if (loc == std::string::npos) {
        def = all_arguments[i].substr(8);
      } else {
        def = all_arguments[i].substr(8, loc - 8);
        value = all_arguments[i].substr(loc + 1);
      }
      if (!def.empty()) {
        SymbolId id = m_symbolTable->registerSymbol(def);
        m_paramList.emplace(id, value);
      }
    } else if (all_arguments[i].find("-I") == 0) {
      fs::path include = all_arguments[i].substr(2);
      if (include.empty()) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_INCLUDE_PATH_DOES_NOT_EXIST, loc);
        m_errors->addError(err);
        break;
      }
      m_includePaths.push_back(mutableSymbolTable()->registerSymbol(
          FileUtils::getPreferredPath(include).string()));
    } else if (all_arguments[i] == "-split") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_SPLIT_FILE_MISSING_SIZE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_nbLinesForFileSplitting = std::stoi(all_arguments[i]);
    } else if (all_arguments[i] == "-cd") {
      i++;
    } else if (all_arguments[i] == "-builtin") {
      i++;
    } else if (all_arguments[i] == "-exe") {
      i++;
      m_exeCommand = all_arguments[i];

    }
// No multiprocess on Windows platform, only multithreads
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
    else if (all_arguments[i] == "-lowmem") {
      std::cerr << "Lowmem option is ignored on this platform\n";
    }
#else
    else if (all_arguments[i] == "-lowmem") {
      m_nbMaxProcesses = 1;
      m_writePpOutput = true;
      m_lowMem = true;
    }
#endif
    else if (all_arguments[i] == "-nouhdm") {
      m_writeUhdm = false;
    } else if (all_arguments[i] == "-mt" || all_arguments[i] == "--threads" ||
               all_arguments[i] == "-mp") {
      bool mt =
          ((all_arguments[i] == "-mt") || (all_arguments[i] == "--threads"));
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_MT_MISSING_LEVEL, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      unsigned int maxMT = 0;
      if (all_arguments[i] == "max") {
        unsigned int concurentThreadsSupported =
            std::thread::hardware_concurrency();
        maxMT = concurentThreadsSupported;
      } else {
        maxMT = std::stoi(all_arguments[i]);
      }
      if (maxMT > 512) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_MT_INCORRECT_LEVEL, loc);
        m_errors->addError(err);
      } else {
        if (m_diff_comp_mode) {
          unsigned int concurentThreadsSupported =
              std::thread::hardware_concurrency();
          if (maxMT > (concurentThreadsSupported / 2))
            maxMT = (concurentThreadsSupported / 2);
        }

        if (maxMT == 0) {
          m_nbMaxTreads = maxMT;

// No multiprocess on Windows platform, only multithreads
#if !(defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__))
          if (!mt) m_nbMaxProcesses = maxMT;
#endif
        } else {
          if (mt) {
            m_nbMaxTreads = maxMT;
            if (m_nbMaxTreads < 2) m_nbMaxTreads = 2;
          } else {
// No multiprocess on Windows platform, only multithreads
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
            m_nbMaxTreads = maxMT;
            if (m_nbMaxTreads < 2) m_nbMaxTreads = 2;
#else
            m_nbMaxProcesses = maxMT;
#endif
          }

          if (profile()) {
            Location loc(mutableSymbolTable()->registerSymbol(
                StrCat(m_nbMaxProcesses, " processes and ", m_nbMaxTreads)));
            Error err(ErrorDefinition::CMD_NUMBER_THREADS, loc);
            m_errors->addError(err);
          }
        }
      }
    } else if (all_arguments[i] == "-strictpythoncheck") {
      PythonAPI::setStrictMode(true);
    } else if (all_arguments[i] == "-tbb") {
      m_useTbb = true;
    } else if ((all_arguments[i] == "--top-module") ||
               (all_arguments[i] == "-top")) {
      i++;
      m_topLevelModules.insert(all_arguments[i]);
    } else if (all_arguments[i] == "-bb_mod") {
      i++;
      m_blackboxModules.insert(all_arguments[i]);
    } else if (all_arguments[i] == "-bb_inst") {
      i++;
      m_blackboxInstances.insert(all_arguments[i]);
    } else if (all_arguments[i] == "-createcache") {
      m_createCache = true;
    } else if (all_arguments[i] == "-lineoffsetascomments") {
      m_lineOffsetsAsComments = true;
    } else if (all_arguments[i] == "-v") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_LIBRARY_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_libraryFiles.push_back(m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string()));
    } else if (all_arguments[i] == "-y") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_LIBRARY_PATH_MISSING_PATH, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_libraryPaths.push_back(m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string()));
    } else if (all_arguments[i] == "-l") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_LOG_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_logFileId = m_symbolTable->registerSymbol(all_arguments[i]);
    } else if (all_arguments[i] == "-L") {
      i++;
      m_orderedLibraries.push_back(m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string()));
    } else if (all_arguments[i] == "-map") {
      i++;
      m_libraryMapFiles.push_back(m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string()));
    } else if (all_arguments[i] == "-cfgfile") {
      i++;
      m_configFiles.push_back(m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string()));
    } else if (all_arguments[i] == "-cfg") {
      i++;
      m_useConfigs.push_back(m_symbolTable->registerSymbol(all_arguments[i]));
    } else if (all_arguments[i] == "-writeppfile") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_writePpOutputFileId = m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string());
    } else if (all_arguments[i] == "-nohash") {
      m_noCacheHash = true;
    } else if (all_arguments[i] == "-cache") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_cacheDirId = m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string());
    } else if (all_arguments[i] == "-replay") {
      m_replay = true;
    } else if (all_arguments[i] == "-writepp") {
      m_writePpOutput = true;
    } else if (all_arguments[i] == "-noinfo") {
      m_info = false;
    } else if (all_arguments[i] == "-nonote") {
      m_note = false;
    } else if (all_arguments[i] == "-nowarning") {
      m_warning = false;
    } else if (all_arguments[i] == "-synth") {
      m_nonSynthesizable = true;
    } else if (all_arguments[i] == "-profile") {
      m_profile = true;
    } else if (all_arguments[i] == "-nobuiltin") {
      m_parseBuiltIn = false;
    } else if (all_arguments[i] == "-outputlineinfo") {
      m_filterFileLine = false;
    } else if (all_arguments[i] == "+liborder") {
      m_liborder = true;
    } else if (all_arguments[i] == "+librescan") {
      m_librescan = true;
    } else if (all_arguments[i] == "+libverbose") {
      m_libverbose = true;
    } else if (all_arguments[i] == "+nolibcell") {
      m_nolibcell = true;
    } else if (all_arguments[i] == "-sverilog") {
      m_sverilog = true;
    } else if (all_arguments[i] == "-fileunit") {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_SEPARATE_COMPILATION_UNIT_ON, loc);
      m_errors->addError(err);
    } else if (all_arguments[i] == "-diffcompunit") {
      if (m_fileunit) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_SEPARATE_COMPILATION_UNIT_ON, loc);
        m_errors->addError(err);
      }
    } else if (all_arguments[i] == "-odir") {
      i++;
    } else if (all_arguments[i] == "--Mdir") {
      i++;
    } else if (all_arguments[i] == "-o") {
      i++;
      m_writePpOutput = true;
      m_parse = true;
    } else if (all_arguments[i] == "-nostdout") {
      m_muteStdout = true;
    } else if (all_arguments[i] == "-verbose") {
      m_verbose = true;
    } else if (all_arguments[i] == "-filterdirectives") {
      m_filterSimpleDirectives = true;
    } else if (all_arguments[i] == "-filterprotected") {
      m_filterProtectedRegions = true;
    } else if (all_arguments[i] == "-filtercomments") {
      m_filterComments = true;
    } else if (all_arguments[i] == "-parse") {
      m_writePpOutput = true;
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
    } else if (all_arguments[i] == "-parseonly") {
      m_writePpOutput = true;
      m_parse = true;
      m_compile = false;
      m_elaborate = false;
      m_parseOnly = true;
    } else if (all_arguments[i] == "-init") {
      m_cacheAllowed = false;
      cleanCache();
    } else if (all_arguments[i] == "-sepcomp") {
      m_sepComp = true;
      m_writePpOutput = true;
      m_parse = true;
      m_compile = false;
      m_elaborate = false;
      m_elabUhdm = false;
      m_writeUhdm = false;
      m_parseBuiltIn = false;
    } else if (all_arguments[i] == "-noparse") {
      m_parse = false;
      m_compile = false;
      m_elaborate = false;
    } else if (all_arguments[i] == "-nocomp") {
      m_compile = false;
      m_elaborate = false;
    } else if (all_arguments[i] == "-noelab") {
      m_elaborate = false;
    } else if (all_arguments[i] == "-elabuhdm") {
      m_elaborate = true;
      m_elabUhdm = true;
    } else if (all_arguments[i] == "-pploc") {
      m_ppOutputFileLocation = true;
    } else if (all_arguments[i] == "-pythonlistener") {
      m_writePpOutput = true;
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
      m_pythonListener = true;
      if (!m_pythonAllowed)
        std::cerr << "ERROR: No Python allowed, check your arguments!\n";
    } else if (all_arguments[i] == "-nopython") {
      m_pythonAllowed = false;
    } else if (all_arguments[i] == "-withpython") {
      withPython();
    } else if (all_arguments[i] == "-pythonevalscriptperfile") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_writePpOutput = true;
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
      m_pythonEvalScriptPerFile = true;
      m_pythonEvalScriptPerFileId =
          m_symbolTable->registerSymbol(all_arguments[i]);
      if (m_pythonAllowed)
        PythonAPI::loadScript(all_arguments[i], true);
      else
        std::cerr << "ERROR: No Python allowed, check your arguments!\n";
    } else if (all_arguments[i] == "-pythonlistenerfile") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_writePpOutput = true;
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
      m_pythonListener = true;
      m_pythonListenerFileId = m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string());
      PythonAPI::setListenerScript(all_arguments[i]);
    } else if (all_arguments[i] == "-pythonevalscript") {
      if (i == all_arguments.size() - 1) {
        Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
        Error err(ErrorDefinition::CMD_PP_FILE_MISSING_FILE, loc);
        m_errors->addError(err);
        break;
      }
      i++;
      m_writePpOutput = true;
      m_parse = true;
      m_compile = true;
      m_elaborate = true;
      m_pythonEvalScript = true;
      m_pythonEvalScriptId = m_symbolTable->registerSymbol(
          FileUtils::getPreferredPath(all_arguments[i]).string());
      if (m_pythonAllowed)
        PythonAPI::loadScript(all_arguments[i], true);
      else
        std::cerr << "ERROR: No Python allowed, check your arguments!\n";
    } else if (all_arguments[i] == "-nocache") {
      m_cacheAllowed = false;
    } else if (all_arguments[i] == "-sv") {
      fs::path svpath = FileUtils::getPreferredPath(all_arguments[i]);
      if (FileUtils::fileExists(svpath)) {
        i++;
        SymbolId id = m_symbolTable->registerSymbol(svpath.string());
        m_sourceFiles.push_back(id);
        fs::path fileName = FileUtils::basename(svpath);
        m_svSourceFiles.insert(fileName);
        fs::path path = FileUtils::getPathName(svpath);
        if (!path.empty()) {
          SymbolId pathId = m_symbolTable->registerSymbol(path.string());
          if (m_includePathSet.find(pathId) == m_includePathSet.end()) {
            m_includePathSet.insert(pathId);
            m_includePaths.push_back(pathId);
          }
        }
      } else {
        m_sverilog = true;
      }
    } else if (all_arguments[i] == "--x-assign") {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_PLUS_ARG_IGNORED, loc);
      m_errors->addError(err);
      i++;
    } else if (all_arguments[i] == "--x-initial") {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_PLUS_ARG_IGNORED, loc);
      m_errors->addError(err);
      i++;
    } else if (all_arguments[i].at(0) == '+') {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_PLUS_ARG_IGNORED, loc);
      m_errors->addError(err);
    } else if (all_arguments[i].at(0) == '-') {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_MINUS_ARG_IGNORED, loc);
      m_errors->addError(err);
    } else if ((all_arguments[i][0] == '-') || is_number(all_arguments[i]) ||
               is_c_file(all_arguments[i]) ||
               (all_arguments[i].rfind(".vlt") != std::string::npos)) {
      Location loc(mutableSymbolTable()->registerSymbol(all_arguments[i]));
      Error err(ErrorDefinition::CMD_PLUS_ARG_IGNORED, loc);
      m_errors->addError(err);
    } else {
      fs::path path = FileUtils::getPreferredPath(all_arguments[i]);
      m_sourceFiles.push_back(m_symbolTable->registerSymbol(path.string()));
      fs::path name = FileUtils::getPathName(path);
      if (!name.empty()) {
        SymbolId pathId = m_symbolTable->registerSymbol(name.string());
        if (m_includePathSet.find(pathId) == m_includePathSet.end()) {
          m_includePathSet.insert(pathId);
          m_includePaths.push_back(pathId);
        }
      }
    }
  }
  status = setupCache_();
  if (!status) return status;

  status = checkCommandLine_();
  return status;
}

bool CommandLineParser::checkCommandLine_() {
  bool noError = true;
  for (const auto& fid : m_sourceFiles) {
    if (!FileUtils::fileExists(m_symbolTable->getSymbol(fid))) {
      Location loc(fid);
      Error err(ErrorDefinition::CMD_VERILOG_FILE_DOES_NOT_EXIST, loc);
      m_errors->addError(err);
      noError = false;
    }
  }
  for (const auto& fid : m_libraryPaths) {
    if (!FileUtils::fileExists(m_symbolTable->getSymbol(fid))) {
      Location loc(fid);
      Error err(ErrorDefinition::CMD_LIBRARY_PATH_DOES_NOT_EXIST, loc);
      m_errors->addError(err);
    }
  }
  for (const auto& fid : m_libraryFiles) {
    if (!FileUtils::fileExists(m_symbolTable->getSymbol(fid))) {
      Location loc(fid);
      Error err(ErrorDefinition::CMD_LIBRARY_FILE_DOES_NOT_EXIST, loc);
      m_errors->addError(err);
      noError = false;
    }
  }
  for (const auto& fid : m_includePaths) {
    if (!FileUtils::fileExists(m_symbolTable->getSymbol(fid))) {
      Location loc(fid);
      Error err(ErrorDefinition::CMD_INCLUDE_PATH_DOES_NOT_EXIST, loc);
      m_errors->addError(err);
    }
  }
  if (!m_errors->printMessages(m_muteStdout)) {
    noError = false;
  }

  return noError;
}

bool CommandLineParser::isSVFile(const fs::path& name) const {
  return m_svSourceFiles.find(name) != m_svSourceFiles.end();
}

bool CommandLineParser::prepareCompilation_(int argc, const char** argv) {
  bool noError = true;
  fs::path odir = m_symbolTable->getSymbol(m_outputDir);

  odir /= m_symbolTable->getSymbol(
      (fileunit() ? m_compileUnitDirectory : m_compileAllDirectory));
  m_fullCompileDir =
      m_symbolTable->registerSymbol(FileUtils::getPreferredPath(odir).string());

  fs::path full_path = odir / m_symbolTable->getSymbol(m_logFileId);
  m_logFileId = m_symbolTable->registerSymbol(
      FileUtils::getPreferredPath(full_path).string());

  if (!FileUtils::mkDirs(odir)) {
    Location loc(m_fullCompileDir);
    Error err(ErrorDefinition::CMD_PP_CANNOT_CREATE_OUTPUT_DIR, loc);
    m_errors->addError(err);
    noError = false;
  }

  m_errors->init();
  logBanner(argc, argv);
  Location loc(m_logFileId);
  Error err(ErrorDefinition::CMD_CREATING_LOG_FILE, loc);
  m_errors->addError(err);

  if (m_errors->hasFatalErrors()) {
    noError = false;
  }

  return noError;
}

bool CommandLineParser::parseBuiltIn() { return m_parseBuiltIn; }

bool CommandLineParser::setupCache_() {
  bool noError = true;
  fs::path cachedir;
  fs::path odir = m_symbolTable->getSymbol(m_outputDir);
  odir /= m_symbolTable->getSymbol(
      (fileunit() ? m_compileUnitDirectory : m_compileAllDirectory));
  if (m_cacheDirId) {
    cachedir = m_symbolTable->getSymbol(m_cacheDirId);
  } else {
    cachedir = odir / m_symbolTable->getSymbol(m_defaultCacheDirId);
    m_cacheDirId = m_symbolTable->registerSymbol(
        FileUtils::getPreferredPath(cachedir).string());
  }

  if (m_cacheAllowed) {
    if (!FileUtils::mkDirs(cachedir)) {
      Location loc(m_cacheDirId);
      Error err(ErrorDefinition::CMD_PP_CANNOT_CREATE_CACHE_DIR, loc);
      m_errors->addError(err);
      noError = false;
    }
  } else {
    FileUtils::rmDirRecursively(cachedir);
  }

  return noError;
}

bool CommandLineParser::cleanCache() {
  bool noError = true;
  fs::path cachedir;
  fs::path odir = m_symbolTable->getSymbol(m_outputDir);
  odir /= m_symbolTable->getSymbol(
      (fileunit() ? m_compileUnitDirectory : m_compileAllDirectory));
  if (m_cacheDirId) {
    cachedir = m_symbolTable->getSymbol(m_cacheDirId);
  } else {
    cachedir = odir / m_symbolTable->getSymbol(m_defaultCacheDirId);
    m_cacheDirId = m_symbolTable->registerSymbol(
        FileUtils::getPreferredPath(cachedir).string());
  }

  if (!m_cacheAllowed) {
    if (!FileUtils::rmDirRecursively(cachedir)) {
      std::cerr << "ERROR: Cannot delete " << cachedir << std::endl;
    }
  }

  return noError;
}
}  // namespace SURELOG

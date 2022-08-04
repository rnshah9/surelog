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
 * File:   CompileProgram.h
 * Author: alain
 *
 * Created on June 6, 2018, 10:43 PM
 */

#ifndef SURELOG_COMPILEPROGRAM_H
#define SURELOG_COMPILEPROGRAM_H
#pragma once

#include <Surelog/DesignCompile/CompileHelper.h>
#include <Surelog/DesignCompile/CompileToolbox.h>

namespace SURELOG {

class CompileDesign;
class Design;
class ErrorContainer;
class Program;
class SymbolTable;

struct FunctorCompileProgram {
  FunctorCompileProgram(CompileDesign* compiler, Program* program,
                        Design* design, SymbolTable* symbols,
                        ErrorContainer* errors)
      : m_compileDesign(compiler),
        m_program(program),
        m_design(design),
        m_symbols(symbols),
        m_errors(errors) {}
  int operator()() const;

 private:
  CompileDesign* const m_compileDesign;
  Program* const m_program;
  Design* const m_design;
  SymbolTable* const m_symbols;
  ErrorContainer* const m_errors;
};

class CompileProgram : public CompileToolbox {
 public:
  CompileProgram(CompileDesign* compiler, Program* program, Design* design,
                 SymbolTable* symbols, ErrorContainer* errors)
      : m_compileDesign(compiler),
        m_program(program),
        m_design(design),
        m_symbols(symbols),
        m_errors(errors) {
    m_helper.seterrorReporting(errors, symbols);
  }

  bool compile();

  ~CompileProgram() override = default;

 private:
  enum CollectType { FUNCTION, DEFINITION, OTHER };
  bool collectObjects_(CollectType collectType);

  CompileDesign* const m_compileDesign;
  Program* const m_program;
  Design* const m_design;
  SymbolTable* const m_symbols;
  ErrorContainer* const m_errors;
  CompileHelper m_helper;
  uint32_t m_nbPorts = 0;
  bool m_hasNonNullPort = false;
  UHDM::VectorOfattribute* m_attributes = nullptr;
};

}  // namespace SURELOG

#endif /* SURELOG_COMPILEPROGRAM_H */

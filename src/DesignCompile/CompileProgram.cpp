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
 * File:   CompileProgram.cpp
 * Author: alain
 *
 * Created on June 6, 2018, 10:43 PM
 */

#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/DesignCompile/CompileDesign.h>
#include <Surelog/DesignCompile/CompileProgram.h>
#include <Surelog/ErrorReporting/Error.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/ErrorReporting/ErrorDefinition.h>
#include <Surelog/ErrorReporting/Location.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Testbench/Program.h>
#include <Surelog/Utils/StringUtils.h>

#include <stack>

namespace SURELOG {

int FunctorCompileProgram::operator()() const {
  CompileProgram* instance = new CompileProgram(m_compileDesign, m_program,
                                                m_design, m_symbols, m_errors);
  instance->compile();
  delete instance;
  return true;
}

bool CompileProgram::compile() {
  const FileContent* fC = m_program->m_fileContents[0];
  NodeId nodeId = m_program->m_nodeIds[0];

  Location loc(m_symbols->registerSymbol(fC->getFileName(nodeId).string()),
               fC->Line(nodeId), fC->Column(nodeId),
               m_symbols->registerSymbol(m_program->getName()));

  Error err1(ErrorDefinition::COMP_COMPILE_PROGRAM, loc);
  ErrorContainer* errors = new ErrorContainer(m_symbols);
  errors->registerCmdLine(
      m_compileDesign->getCompiler()->getCommandLineParser());
  errors->addError(err1);
  errors->printMessage(
      err1,
      m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());
  delete errors;

  Error err2(ErrorDefinition::COMP_PROGRAM_OBSOLETE_USAGE, loc);
  m_errors->addError(err2);

  if (!collectObjects_(CollectType::FUNCTION)) return false;
  if (!collectObjects_(CollectType::DEFINITION)) return false;
  if (!collectObjects_(CollectType::OTHER)) return false;

  return true;
}

bool CompileProgram::collectObjects_(CollectType collectType) {
  const FileContent* fC = m_program->m_fileContents[0];
  NodeId nodeId = m_program->m_nodeIds[0];
  std::vector<VObjectType> stopPoints = {
      VObjectType::slClass_declaration,
  };

  NodeId programId = m_program->m_nodeIds[0];
  do {
    VObject current = fC->Object(programId);
    programId = current.m_child;
  } while (programId &&
           (fC->Type(programId) != VObjectType::slAttribute_instance));
  if (programId) {
    UHDM::VectorOfattribute* attributes =
        m_helper.compileAttributes(m_program, fC, programId, m_compileDesign);
    m_program->Attributes(attributes);
  }

  if (fC->getSize() == 0) return true;
  VObject current = fC->Object(nodeId);
  NodeId id = current.m_child;
  if (!id) id = current.m_sibling;
  if (!id) return false;

  if (collectType == CollectType::FUNCTION) {
    // Package imports
    std::vector<FileCNodeId> pack_imports;
    // - Local file imports
    for (auto import : fC->getObjects(VObjectType::slPackage_import_item)) {
      pack_imports.push_back(import);
    }

    for (auto pack_import : pack_imports) {
      const FileContent* pack_fC = pack_import.fC;
      NodeId pack_id = pack_import.nodeId;
      m_helper.importPackage(m_program, m_design, pack_fC, pack_id,
                             m_compileDesign);
    }
  }

  NodeId ParameterPortListId;
  std::stack<NodeId> stack;
  stack.push(id);
  VObjectType port_direction = VObjectType::slNoType;
  while (!stack.empty()) {
    id = stack.top();
    if (ParameterPortListId && (id == ParameterPortListId)) {
      ParameterPortListId = InvalidNodeId;
    }
    stack.pop();
    current = fC->Object(id);
    VObjectType type = fC->Type(id);
    switch (type) {
      case VObjectType::slPackage_import_item: {
        if (collectType != CollectType::FUNCTION) break;
        m_helper.importPackage(m_program, m_design, fC, id, m_compileDesign);
        m_helper.compileImportDeclaration(m_program, fC, id, m_compileDesign);
        break;
      }
      case VObjectType::slParameter_port_list: {
        if (collectType != CollectType::DEFINITION) break;
        ParameterPortListId = id;
        NodeId list_of_param_assignments = fC->Child(id);
        if (list_of_param_assignments)
          m_helper.compileParameterDeclaration(
              m_program, fC, list_of_param_assignments, m_compileDesign, false,
              nullptr, false, false, false);
        break;
      }
      case VObjectType::slAnsi_port_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compileAnsiPortDeclaration(m_program, fC, id, port_direction);
        break;
      }
      case VObjectType::slPort: {
        if (fC->Child(id)) {
          m_hasNonNullPort = true;
        }
        if (collectType == CollectType::FUNCTION) m_nbPorts++;
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compilePortDeclaration(m_program, fC, id, m_compileDesign,
                                        port_direction,
                                        m_hasNonNullPort || (m_nbPorts > 1));
        break;
      }
      case VObjectType::slTask_declaration: {
        // Called twice, placeholder first, then definition
        if (collectType == CollectType::OTHER) break;
        m_helper.compileTask(m_program, fC, id, m_compileDesign, nullptr);
        break;
      }
      case VObjectType::slFunction_declaration: {
        // Called twice, placeholder first, then definition
        if (collectType == CollectType::OTHER) break;
        m_helper.compileFunction(m_program, fC, id, m_compileDesign, nullptr);
        break;
      }
      case VObjectType::slLet_declaration: {
        if (collectType != CollectType::FUNCTION) break;
        m_helper.compileLetDeclaration(m_program, fC, id, m_compileDesign);
        break;
      }
      case VObjectType::slInput_declaration:
      case VObjectType::slOutput_declaration:
      case VObjectType::slInout_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compilePortDeclaration(m_program, fC, id, m_compileDesign,
                                        port_direction, m_hasNonNullPort);
        break;
      }
      case VObjectType::slPort_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compilePortDeclaration(m_program, fC, id, m_compileDesign,
                                        port_direction, m_hasNonNullPort);
        break;
      }
      case VObjectType::slContinuous_assign: {
        if (collectType != CollectType::OTHER) break;
        m_helper.compileContinuousAssignment(m_program, fC, fC->Child(id),
                                             m_compileDesign, nullptr);
        break;
      }
      case VObjectType::slParameter_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        NodeId list_of_type_assignments = fC->Child(id);
        if (fC->Type(list_of_type_assignments) == slList_of_type_assignments ||
            fC->Type(list_of_type_assignments) == slType) {
          // Type param
          m_helper.compileParameterDeclaration(
              m_program, fC, list_of_type_assignments, m_compileDesign, false,
              nullptr, ParameterPortListId, false, false);

        } else {
          m_helper.compileParameterDeclaration(
              m_program, fC, id, m_compileDesign, false, nullptr,
              ParameterPortListId, false, false);
        }
        break;
      }
      case VObjectType::slLocal_parameter_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        NodeId list_of_type_assignments = fC->Child(id);
        if (fC->Type(list_of_type_assignments) == slList_of_type_assignments ||
            fC->Type(list_of_type_assignments) == slType) {
          // Type param
          m_helper.compileParameterDeclaration(
              m_program, fC, list_of_type_assignments, m_compileDesign, true,
              nullptr, ParameterPortListId, false, false);

        } else {
          m_helper.compileParameterDeclaration(
              m_program, fC, id, m_compileDesign, true, nullptr,
              ParameterPortListId, false, false);
        }
        break;
      }
      case VObjectType::slClass_declaration: {
        if (collectType != CollectType::OTHER) break;
        NodeId nameId = fC->Child(id);
        if (fC->Type(nameId) == slVirtual) {
          nameId = fC->Sibling(nameId);
        }
        std::string name = fC->SymName(nameId);
        FileCNodeId fnid(fC, nameId);
        m_program->addObject(type, fnid);

        std::string completeName = m_program->getName() + "::" + name;

        DesignComponent* comp = fC->getComponentDefinition(completeName);

        m_program->addNamedObject(name, fnid, comp);
        break;
      }
      case VObjectType::slNet_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compileNetDeclaration(m_program, fC, id, false,
                                       m_compileDesign);
        m_attributes = nullptr;
        break;
      }
      case VObjectType::slData_declaration: {
        if (collectType != CollectType::DEFINITION) break;
        m_helper.compileDataDeclaration(m_program, fC, id, false,
                                        m_compileDesign, false, m_attributes);
        m_attributes = nullptr;
        break;
      }
      case VObjectType::slAttribute_instance: {
        if (collectType != CollectType::DEFINITION) break;
        m_attributes =
            m_helper.compileAttributes(m_program, fC, id, m_compileDesign);
        break;
      }
      case VObjectType::slInitial_construct:
        if (collectType != CollectType::OTHER) break;
        m_helper.compileInitialBlock(m_program, fC, id, m_compileDesign);
        break;
      case VObjectType::slFinal_construct:
        if (collectType != CollectType::OTHER) break;
        m_helper.compileFinalBlock(m_program, fC, id, m_compileDesign);
        break;
      case VObjectType::slParam_assignment:
      case VObjectType::slDefparam_assignment: {
        if (collectType != CollectType::DEFINITION) break;
        FileCNodeId fnid(fC, id);
        m_program->addObject(type, fnid);
        break;
      }
      case VObjectType::slDpi_import_export: {
        if (collectType != CollectType::FUNCTION) break;
        NodeId Import = fC->Child(id);
        NodeId StringLiteral = fC->Sibling(Import);
        NodeId Context_keyword = fC->Sibling(StringLiteral);
        NodeId Task_prototype;
        if (fC->Type(Context_keyword) == slContext_keyword)
          Task_prototype = fC->Sibling(Context_keyword);
        else
          Task_prototype = Context_keyword;
        if (fC->Type(Task_prototype) == slTask_prototype) {
          Task* task =
              m_helper.compileTaskPrototype(m_program, fC, id, m_compileDesign);
          m_program->insertTask(task);
        } else {
          Function* func = m_helper.compileFunctionPrototype(m_program, fC, id,
                                                             m_compileDesign);
          m_program->insertFunction(func);
        }
        break;
      }
      case VObjectType::slStringConst: {
        if (collectType != CollectType::DEFINITION) break;
        NodeId sibling = fC->Sibling(id);
        if (!sibling) {
          if (fC->Type(fC->Parent(id)) != slProgram_declaration) break;
          const std::string& endLabel = fC->SymName(id);
          std::string moduleName = m_program->getName();
          moduleName = StringUtils::ltrim(moduleName, '@');
          if (endLabel != moduleName) {
            Location loc(
                m_compileDesign->getCompiler()
                    ->getSymbolTable()
                    ->registerSymbol(
                        fC->getFileName(m_program->getNodeIds()[0]).string()),
                fC->Line(m_program->getNodeIds()[0]),
                fC->Column(m_program->getNodeIds()[0]),
                m_compileDesign->getCompiler()
                    ->getSymbolTable()
                    ->registerSymbol(moduleName));
            Location loc2(m_compileDesign->getCompiler()
                              ->getSymbolTable()
                              ->registerSymbol(fC->getFileName(id).string()),
                          fC->Line(id), fC->Column(id),
                          m_compileDesign->getCompiler()
                              ->getSymbolTable()
                              ->registerSymbol(endLabel));
            Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
            m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
          }
        }
        break;
      }
      default:
        break;
    }

    if (current.m_sibling) stack.push(current.m_sibling);
    if (current.m_child) {
      if (!stopPoints.empty()) {
        bool stop = false;
        for (auto t : stopPoints) {
          if (t == current.m_type) {
            stop = true;
            break;
          }
        }
        if (!stop)
          if (current.m_child) stack.push(current.m_child);
      } else {
        if (current.m_child) stack.push(current.m_child);
      }
    }
  }
  return true;
}

}  // namespace SURELOG

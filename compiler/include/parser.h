/*
 * Copyright 2020-2022 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _PARSER_H_
#define _PARSER_H_

class BlockStmt;
class VisibilityStmt;

#include "symbol.h"

extern int         chplLineno;
extern bool        chplParseString;
extern const char* chplParseStringMsg;

extern ModTag      currentModuleType;
extern const char* currentModuleName;

extern int         yystartlineno;
extern const char* yyfilename;
extern BlockStmt*  yyblock;

extern bool parsingPrivate;

extern bool countTokens;
extern bool printTokens;

void               parse();

void addInternalModulePath(const ArgumentDescription* desc,
                           const char* newpath);
void addStandardModulePath(const ArgumentDescription* desc,
                           const char* newpath);

void               setupModulePaths();

void               addFlagModulePath(const char* newpath);

void               addModuleToParseList(const char* name,
                                        VisibilityStmt* newUse);

// The new parser does not rely on yyfilename to set locations, so passing
// in the submodule path allows for overriding that behavior.
ModuleSymbol*      parseIncludedSubmodule(const char* name,
                                          const char* path=yyfilename);

void noteParsedIncludedModule(ModuleSymbol* mod, const char* path);

#endif

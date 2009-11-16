/*--------------------------------------------------------------------
  (C) Copyright 2006-2009 Barcelona Supercomputing Center 
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.
  
  Mercurium C/C++ source-to-source compiler is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more
  details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with Mercurium C/C++ source-to-source compiler; if
  not, write to the Free Software Foundation, Inc., 675 Mass Ave,
  Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/

#include "tl-basic_test.hpp"
#include "tl-ast.hpp"
#include "tl-scopelink.hpp"
#include "tl-source.hpp"
#include <iostream>

namespace TL
{
    BasicTestPhase::BasicTestPhase()
    {
        std::cerr << "Basic test phase created" << std::endl;
    }

    void BasicTestPhase::pre_run(TL::DTO& dto)
    {
        std::cerr << "Basic test phase pre_run" << std::endl;

        AST_t ast = dto["translation_unit"];
        ScopeLink sl = dto["scope_link"];

        Source src;

        src << "extern void f(int n);"
            ;

        src.parse_global(ast, sl);
    }

    void BasicTestPhase::run(TL::DTO& dto)
    {
        std::cerr << "Basic test phase run" << std::endl;
    }
}

EXPORT_PHASE(TL::BasicTestPhase);

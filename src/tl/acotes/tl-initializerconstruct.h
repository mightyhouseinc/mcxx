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

// 
// File:   tl-initializerconstruct.h
// Author: drodenas
//
// Created on 26 / desembre / 2007, 13:06
//

#ifndef _TL_INITIALIZERCONSTRUCT_H
#define	_TL_INITIALIZERCONSTRUCT_H


#include <tl-langconstruct.hpp>
#include <tl-pragmasupport.hpp>

namespace TL { namespace Acotes {
    
    class InitializerConstruct
    : TL::PragmaCustomConstruct
    {
    // -- LangConstruct support
    public:
        InitializerConstruct(TL::LangConstruct langConstruct);
    private:
        TL::LangConstruct getBody();
        TL::LangConstruct getConstruct();

    // -- CompilerPhase events
    public:
        void onPre();
        void onPost();
    };
    
} /* end namespace Acotes */ } /* end namespace TL */


#endif	/* _TL_INITIALIZERCONSTRUCT_H */


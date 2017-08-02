/*--------------------------------------------------------------------
  (C) Copyright 2006-2013 Barcelona Supercomputing Center
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
  See AUTHORS file in the top level directory for information
  regarding developers and contributors.
  
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



/*
<testinfo>
test_generator=(config/mercurium-ompss config/mercurium-ompss-v2)
test_nolink=yes
</testinfo>
*/
#include <stdlib.h>

typedef struct
{
    int nz;
    int diag;
} X;

int main(void)
{
    int i;
    X foo;
    foo.diag = 3;
    int bar[40];
#pragma omp task inout(bar[0:foo.diag])
    for(i=0;i<foo.diag;i++)
    {
        bar[i] = i;
    }

#pragma omp taskwait

    if (bar[0] != 0) abort();
    if (bar[1] != 1) abort();
    if (bar[2] != 2) abort();

    return 0;
}

/*  Pargen - Flexible parser generator
    Copyright (C) 2011 Dmitry Shatrov

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#ifndef __PARGEN__SOURCE_COMPILER_H__
#define __PARGEN__SOURCE_COMPILER_H__

#include <mycpp/list.h>
#include <mycpp/file.h>

#include <pargen/declarations.h>
#include <pargen/pargen_task_parser.h>
#include <pargen/compile.h>
#include <pargen/compilation_exception.h>

namespace Pargen {

using namespace MyCpp;

void compileSource (File *file,
		    PargenTask const *pargen_task,
		    CompilationOptions const *opts)
	     throw (CompilationException,
		    IOException,
		    InternalException);

}

#endif /* __PARGEN__SOURCE_COMPILER_H__ */


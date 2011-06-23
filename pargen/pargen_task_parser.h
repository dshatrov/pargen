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


#ifndef __PARGEN__PARGEN_TASK_PARSER_H__
#define __PARGEN__PARGEN_TASK_PARSER_H__

#include <mycpp/mycpp.h>

#include <mylang/token_stream.h>

#include <pargen/parsing_exception.h>
#include <pargen/declarations.h>

namespace Pargen {

using namespace MyCpp;

class PargenTask : public SimplyReferenced
{
public:
    List< Ref<Declaration> > decls;
};

Ref<PargenTask> parsePargenTask (MyLang::TokenStream *token_stream)
			  throw (Pargen::ParsingException,
				 InternalException);

void dumpDeclarations (PargenTask const *pargen_task);

}

#endif /* __PARGEN__PARGEN_TASK_PARSER_H__ */


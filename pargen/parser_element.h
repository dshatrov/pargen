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


#ifndef __PARGEN__PARSER_ELEMENT_H__
#define __PARGEN__PARSER_ELEMENT_H__

#include <mycpp/object.h>

namespace Pargen {

using namespace MyCpp;

class ParserElement // Deprecated : public virtual SimplyReferenced
{
public:
    void *user_obj;

    // TODO This seems to be unused
    static bool testType (ParserElement * /* parser_element */)
    {
	return true;
    }

    ParserElement ()
	: user_obj (NULL)
    {
    }
};

class ParserElement_Token : public ParserElement,
			    public virtual SimplyReferenced
{
public:
    ConstMemoryDesc token;

    ParserElement_Token (ConstMemoryDesc const &token,
			 void *user_obj)
	: token (token)
    {
	this->user_obj = user_obj;
    }
};

}

#endif /* __PARGEN__PARSER_ELEMENT_H__ */


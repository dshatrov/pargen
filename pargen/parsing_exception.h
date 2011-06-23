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


#ifndef __PARGEN__PARSING_EXCEPTION_H__
#define __PARGEN__PARSING_EXCEPTION_H__

#include <mycpp/internal_exception.h>

#include <mylang/file_position.h>

namespace Pargen {

/*c*/
class ParsingException : public MyCpp::InternalException,
			 public MyCpp::ExceptionBase <ParsingException>
{
public:
    const MyLang::FilePosition fpos;

    ParsingException (MyLang::FilePosition const &fpos,
		      MyCpp::String      *message = MyCpp::String::nullString (),
		      MyCpp::Exception   *cause = NULL)
	: InternalException (message, cause),
	  fpos (fpos)
    {
    }
};

}

#endif /* __PARGEN__PARSING_EXCEPTION_H__ */


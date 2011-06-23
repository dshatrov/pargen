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


#ifndef __PARGEN__COMPILE_H__
#define __PARGEN__COMPILE_H__

#include <mycpp/object.h>
#include <mycpp/string.h>

namespace Pargen {

using namespace MyCpp;

class CompilationOptions : public SimplyReferenced
{
public:
    Ref<String> module_name;
    Ref<String> capital_module_name;
    Ref<String> all_caps_module_name;

    Ref<String> header_name;
    Ref<String> capital_header_name;
    Ref<String> all_caps_header_name;
};

}

#endif /* __PARGEN__COMPILE_H__ */


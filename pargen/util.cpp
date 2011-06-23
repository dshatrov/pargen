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


#include <pargen/util.h>

using namespace MyCpp;

namespace Pargen {

static char
capitalizeSymbol (char s)
{
    if (s >= 'a' && s <= 'z') {
	if ('A' > 'a')
	    s += 'A' - 'a';
	else
	    s -= 'a' - 'A';
    }

    return s;
}

static char
lowercaseSymbol (char s)
{
    if (s >= 'A' && s <= 'Z') {
	if ('A' > 'a')
	    s -= 'A' - 'a';
	else
	    s += 'a' - 'A';
    }

    return s;
}

Ref<String>
capitalizeName (ConstMemoryDesc const &name,
		bool keep_underscore)
{
    if (name.getLength () == 0)
	return String::nullString ();

    abortIf (name.getMemory () == NULL);

    Size str_len = 0;
    for (Size i = 0; i < name.getLength (); i++) {
	if (name.getMemory () [i] == ' ' ||
	    name.getMemory () [i] == '-')
	{
	    continue;
	}

	if (name.getMemory () [i] == '_' && !keep_underscore)
	    continue;

	str_len ++;
    }

    if (str_len == 0)
	return String::nullString ();

    Ref<String> str = grab (new String);
    str->allocate (str_len);

    Bool capital = true;
    for (Size i = 0, j = 0; i < name.getLength (); i++) {
	if (name.getMemory () [i] == ' ' ||
	    name.getMemory () [i] == '-')
	{
	    capital = true;
	    continue;
	}

	if (name.getMemory () [i] == '_') {
	    if (keep_underscore) {
		str->getData () [j] = name.getMemory () [i];
		j++;
	    }
	    capital = true;
	    continue;
	}

	abortIf (j >= str_len);

	if (capital) {
	    str->getData () [j] = capitalizeSymbol (name.getMemory () [i]);
	    capital = false;
	} else
	    str->getData () [j] = name.getMemory () [i];

	j++;
    }

    str->getData () [str_len] = 0;

    return str;
}

Ref<String>
capitalizeNameAllCaps (ConstMemoryDesc const &name)
{
    if (name.getLength () == 0)
	return String::nullString ();

    abortIf (name.getMemory () == NULL);

    Ref<String> str = grab (new String);
    str->allocate (name.getLength ());

    for (Size i = 0; i < name.getLength (); i++)
	str->getData () [i] = capitalizeSymbol (name.getMemory () [i]);

    str->getData () [name.getLength ()] = 0;

    return str;
}

Ref<String>
lowercaseName (ConstMemoryDesc const &name)
{
    if (name.getLength () == 0)
	return String::nullString ();

    abortIf (name.getMemory () == NULL);

    Ref<String> str = grab (new String);
    str->allocate (name.getLength ());

    for (Size i = 0; i < name.getLength (); i++) {
	if (name.getMemory () [i] == '-')
	    str->getData () [i] = '_';
	else
	    str->getData () [i] = lowercaseSymbol (name.getMemory () [i]);
    }

    str->getData () [name.getLength ()] = 0;

    return str;
}

}


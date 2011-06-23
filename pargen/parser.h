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


#ifndef __PARGEN__PARSER_H__
#define __PARGEN__PARSER_H__

#include <mylang/token_stream.h>

#include <pargen/grammar.h>
#include <pargen/parser_element.h>
#include <pargen/lookup_data.h>
#include <pargen/parsing_exception.h>

/**
 * .root
 * .title Pargen
 */

namespace Pargen {

using namespace MyCpp;

/*c
 * Position marker
 */
class ParserPositionMarker : public virtual SimplyReferenced
{
};

/*c
 * Parser control object
 */
class ParserControl : public virtual SimplyReferenced
{
public:
    virtual void setCreateElements (bool create_elements) = 0;

    virtual Ref<ParserPositionMarker> getPosition () = 0;

    virtual void setPosition (ParserPositionMarker *pmark) = 0;

    virtual void setVariant (ConstMemoryDesc const &variant_name) = 0;
};

// External users should not modify contents of ParserConfig objects.
class ParserConfig : public virtual SimplyReferenced
{
public:
    bool upwards_jumps;
};

Ref<ParserConfig> createParserConfig (bool upwards_jumps);

Ref<ParserConfig> createDefaultParserConfig ();

/*m*/
void optimizeGrammar (Grammar *grammar /* non-null */);

/*m*/
void parse (MyLang::TokenStream  *token_stream,
	    LookupData           *lookup_data,
	    void                 *user_data,
	    Grammar              *grammar,
	    ParserElement       **ret_element,
	    ConstMemoryDesc const &default_variant = ConstMemoryDesc::forString ("default"),
	    ParserConfig         *parser_config = NULL,
	    bool                  debug_dump = false)
     throw (ParsingException,
	    IOException,
	    InternalException);

}

#endif /* __PARGEN__PARSER_H__ */


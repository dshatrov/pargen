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


#ifndef __PARGEN__DECLARATIONS_H__
#define __PARGEN__DECLARATIONS_H__

#include <mycpp/mycpp.h>

namespace Pargen {

using namespace MyCpp;

// Declaration

class Declaration : public SimplyReferenced
{
public:
    enum Type {
	t_Phrases,
	t_Callbacks
//	t_Alias
    };

    const Type declaration_type;

    Ref<String> declaration_name;
    Ref<String> lowercase_declaration_name;

    Declaration (Type type)
	: declaration_type (type)
    {
    }
};

// Declaration
//     Declaration_Callbacks
//         CallbackDecl

class CallbackDecl : public SimplyReferenced
{
public:
    Ref<String> callback_name;
};

// Declaration
//      Declaration_Phrases

class Phrase;

class Declaration_Phrases : public Declaration
{
public:
    class PhraseRecord : public SimplyReferenced
    {
    public:
	Ref<Phrase> phrase;
	List< Ref<String> > variant_names;
    };

#if 0
    Ref<String> begin_cb_name;
    Bool begin_cb_repetition;
#endif

    Bool is_alias;
    Ref<String> aliased_name;
    Ref<String> deep_aliased_name;
    Declaration_Phrases *aliased_decl;

    List< Ref<PhraseRecord> > phrases;

    // Used for detecting infinite grammar loops when linking upwards anchors.
    Size loop_id;
    Size decl_loop_id;

    Map< Ref<CallbackDecl>,
	 MemberExtractor< CallbackDecl,
			  Ref<String>,
			  &CallbackDecl::callback_name,
			  MemoryDesc,
			  AccessorExtractor< String,
					     MemoryDesc,
					     &String::getMemoryDesc > >,
	 MemoryComparator<> >
	    callbacks;

    Declaration_Phrases ()
	: Declaration (Declaration::t_Phrases),
	  aliased_decl (NULL),
	  loop_id (0),
	  decl_loop_id (0)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase

class PhrasePart;

class Phrase : public SimplyReferenced
{
public:
    Ref<String> phrase_name;
    List< Ref<PhrasePart> > phrase_parts;
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart

class PhrasePart : public SimplyReferenced
{
public:
    enum Type {
	t_Phrase,
	t_Token,
	t_AcceptCb,
	t_UniversalAcceptCb,
	t_UpwardsAnchor,
	t_Label
    };

    const Type phrase_part_type;

    Bool seq;
    Bool opt;

    Ref<String> name;
    Bool name_is_explicit;

    Ref<String> toString ();

    PhrasePart (Type type)
	: phrase_part_type (type)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_Phrase

class PhrasePart_Phrase : public PhrasePart
{
public:
    Ref<String> phrase_name;
    Ref<Declaration_Phrases> decl_phrases;

    PhrasePart_Phrase ()
	: PhrasePart (PhrasePart::t_Phrase)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_Token

class PhrasePart_Token : public PhrasePart
{
public:
    // If null, then any token matches.
    Ref<String> token;
    Ref<String> token_match_cb;

    PhrasePart_Token ()
	: PhrasePart (PhrasePart::t_Token)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_AcceptCb

class PhrasePart_AcceptCb : public PhrasePart
{
public:
    Ref<String> cb_name;
    Bool repetition;

    PhrasePart_AcceptCb ()
	: PhrasePart (PhrasePart::t_AcceptCb)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_UniversalAcceptCb

class PhrasePart_UniversalAcceptCb : public PhrasePart
{
public:
    Ref<String> cb_name;
    Bool repetition;

    PhrasePart_UniversalAcceptCb ()
	: PhrasePart (PhrasePart::t_UniversalAcceptCb)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_UpwardsAnchor

class PhrasePart_UpwardsAnchor : public PhrasePart
{
public:
    Ref<String> declaration_name;
    Ref<String> phrase_name;
    Ref<String> label_name;
    Ref<String> jump_cb_name;

    Size switch_grammar_index;
    Size compound_grammar_index;

    PhrasePart_UpwardsAnchor ()
	: PhrasePart (PhrasePart::t_UpwardsAnchor),
	  switch_grammar_index (0),
	  compound_grammar_index (0)
    {
    }
};

// Declaration
//     Declaration_Phrases
//         Phrase
//             PhrasePart
//                 PhrasePart_UpwardsAnchor

class PhrasePart_Label : public PhrasePart
{
public:
    Ref<String> label_name;

    PhrasePart_Label ()
	: PhrasePart (PhrasePart::t_Label)
    {
    }
};

// Declaration
//     Declaration_Callbacks

class Declaration_Callbacks : public Declaration
{
public:
    List< Ref<CallbackDecl> > callbacks;

    Declaration_Callbacks ()
	: Declaration (Declaration::t_Callbacks)
    {
    }
};

#if 0
// Declaration
//     Declaration_Alias

class Declaration_Alias : public Declaration
{
public:
    Ref<String> aliased_name;

    Declaration_Alias ()
	: Declaration (Declaration::t_Alias)
    {
    }
};
#endif

}

#endif /* __PARGEN__DECLARATIONS_H__ */


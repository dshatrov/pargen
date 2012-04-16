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


#include <mycpp/io.h>

#include <pargen/pargen_task_parser.h>
#include <pargen/util.h>


#define DEBUG(a) ;
#define DEBUG_ANCHORS(a) a
#define DEBUG_OLD(a) ;

#define FUNC_NAME(a) a


using namespace MyCpp;
using namespace MyLang;

namespace Pargen {

namespace Parse_priv {}
using namespace Parse_priv;

namespace Parse_priv {

class LookupData : public SimplyReferenced
{
public:
//protected:
    class LookupNode : public SimplyReferenced
    {
    public:
	Ref<String> name;
	Ref<Declaration_Phrases> decl_phrases;
    };

    Map< Ref<LookupNode>,
	 MemberExtractor< LookupNode,
			  Ref<String>,
			  &LookupNode::name,
			  MemoryDesc,
			  AccessorExtractor< String,
					     MemoryDesc,
					     &String::getMemoryDesc > >,
	 MemoryComparator<> >
	    lookup_map;

public:
    // Returns false if there is a phrase with such name already.
    bool addDeclaration (Declaration_Phrases *decl_phrases,
			 ConstMemoryDesc const &name)
    {
	if (!lookup_map.lookup (name).isNull ())
	    return false;

	Ref<LookupNode> lookup_node = grab (new LookupNode);
	lookup_node->name = grab (new String (name));
	lookup_node->decl_phrases = decl_phrases;

	lookup_map.add (lookup_node);

	return true;
    }

    Ref<Declaration_Phrases> lookupDeclaration (ConstMemoryDesc const &name,
						Bool *ret_is_alias)
    {
	if (ret_is_alias != NULL)
	    *ret_is_alias = false;

	Declaration_Phrases *ret_decl_phrases = NULL;

	{
	    ConstMemoryDesc cur_name = name;
	    for (;;) {
		DEBUG_OLD (
		    errf->print ("--- lookup: ").print (cur_name).pendl ();
		)
		MapBase< Ref<LookupNode> >::Entry map_entry = lookup_map.lookup (cur_name);
		if (map_entry.isNull ())
		    break;

		abortIf (map_entry.getData ()->decl_phrases.isNull ());
		Declaration_Phrases *decl_phrases = map_entry.getData ()->decl_phrases;
		if (!decl_phrases->is_alias) {
		    ret_decl_phrases = decl_phrases;
		    break;
		}

		if (ret_is_alias != NULL)
		    *ret_is_alias = true;

		DEBUG_OLD (
		    errf->print ("--- lookupDeclaration: alias: ").print (cur_name).pendl ();
		)
		cur_name = decl_phrases->aliased_name->getMemoryDesc ();
	    }
	}

	return ret_decl_phrases;
    }
};

}

static ConstMemoryDesc
getNonwhspToken (TokenStream *token_stream)
{
    for (;;) {
	ConstMemoryDesc token = token_stream->getNextToken ();
	if (token.getLength () == 0)
	    return ConstMemoryDesc ();

	DEBUG ( errf->print ("Pargen.(PargenTaskParser).getNonwhspToken: token: ").print (token).pendl (); )

	if (compareByteArrays (token, ConstMemoryDesc::forString ("\n")) == ComparisonEqual ||
	    compareByteArrays (token, ConstMemoryDesc::forString ("\t")) == ComparisonEqual ||
	    compareByteArrays (token, ConstMemoryDesc::forString (" "))  == ComparisonEqual)
	{
	    continue;
	}

	// Skipping comments
	if (compareByteArrays (token, ConstMemoryDesc::forString ("#")) == ComparisonEqual) {
	    for (;;) {
		token = token_stream->getNextToken ();
		if (token.getLength () == 0)
		    return NULL;

		DEBUG ( errf->print ("Pargen.(PargenTaskParser).getNonwhspToken: token (#): ").print (token).pendl (); )

		if (compareByteArrays (token, ConstMemoryDesc::forString ("\n")) == ComparisonEqual)
		    break;
	    }

	    continue;
	}

	return token;
    }

    abortIfReached ();
    return ConstMemoryDesc ();
}

static ConstMemoryDesc
getNextToken (TokenStream *token_stream)
{
    ConstMemoryDesc token = token_stream->getNextToken ();
    if (token.getLength () == 0)
	return NULL;

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).getNextToken: "
		     "token: ").print (token).pendl ();
    )

    // Skipping comments
    if (compareByteArrays (token, ConstMemoryDesc::forString ("#")) == ComparisonEqual) {
	for (;;) {
	    token = token_stream->getNextToken ();
	    if (token.getLength () == 0)
		return NULL;

	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).getNextToken: "
			     "token (#): ").print (token).pendl ();
	    )

	    if (TokenStream::isNewline (token))
		return ConstMemoryDesc::forString ("\n");
	}
    }

    return token;
}

static Ref<PhrasePart>
parsePhrasePart (TokenStream *token_stream,
		 LookupData  *lookup_data)
    throw (ParsingException)
{
    abortIf (token_stream == NULL ||
	     lookup_data == NULL);

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parsePhrasePart").pendl ();
    )

    TokenStream::PositionMarker marker;
    token_stream->getPosition (&marker);

{
    Ref<PhrasePart> phrase_part;

    // Phrase part name specialization ("<name>").
    Ref<String> part_name;

    bool vertical_equivalence = false;
    for (;;) {
	DEBUG (
	    errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: iteration").pendl ();
	)

//	errf->print (">");

	ConstMemoryDesc token = getNonwhspToken (token_stream);
	if (token.getLength () == 0) {
//	    errf->print ("W");
	    goto _no_match;
	}

	vertical_equivalence = false;

	if (compareByteArrays (token, ConstMemoryDesc::forString (")")) == ComparisonEqual ||
	    compareByteArrays (token, ConstMemoryDesc::forString ("|")) == ComparisonEqual)
	{
//	    errf->print ("1");
	    goto _no_match;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("*")) == ComparisonEqual) {
	  // Any token

	    Ref<PhrasePart_Token> phrase_part__token = grab (new PhrasePart_Token);
	    phrase_part = phrase_part__token;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("{")) == ComparisonEqual) {
	  // Any token with a match callback

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    Ref<String> token_match_cb = grab (new String (token));

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    if (compareByteArrays (token, ConstMemoryDesc::forString ("}")) != ComparisonEqual)
		goto _no_match;

	    Ref<PhrasePart_Token> phrase_part__token = grab (new PhrasePart_Token);
	    phrase_part__token->token_match_cb = token_match_cb;

	    phrase_part = phrase_part__token;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("(")) == ComparisonEqual) {
	  // TODO Vertical equivalence

	    Ref<String> declaration_name;
	    Ref<String> phrase_name;
	    Ref<String> label_name;
	    Ref<String> jump_cb_name;

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    declaration_name = capitalizeName (token);

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    if (compareByteArrays (token, ConstMemoryDesc::forString (":")) == ComparisonEqual) {
		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;

		phrase_name = grab (new String (token));

		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;
	    }

	    if (compareByteArrays (token, ConstMemoryDesc::forString ("@")) != ComparisonEqual)
		goto _no_match;

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    label_name = grab (new String (token));

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    if (compareByteArrays (token, ConstMemoryDesc::forString (")")) != ComparisonEqual) {
		jump_cb_name = grab (new String (token));

		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;

		if (compareByteArrays (token, ConstMemoryDesc::forString (")")) != ComparisonEqual)
		    goto _no_match;
	    }

	    Ref<PhrasePart_UpwardsAnchor> phrase_part__upwards_anchor = grab (new PhrasePart_UpwardsAnchor);
	    phrase_part__upwards_anchor->declaration_name = declaration_name;
	    phrase_part__upwards_anchor->phrase_name = phrase_name;
	    phrase_part__upwards_anchor->label_name = label_name;
	    phrase_part__upwards_anchor->jump_cb_name = jump_cb_name;
	    phrase_part = phrase_part__upwards_anchor;

	    vertical_equivalence = true;

//	    errf->print ("v");

	    token_stream->getPosition (&marker);

// Deprecated	    continue;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("@")) == ComparisonEqual) {
	  // Label

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    Ref<String> label_name = grab (new String (token));

	    Ref<PhrasePart_Label> phrase_part__label = grab (new PhrasePart_Label);
	    phrase_part__label->label_name = label_name;
	    phrase_part = phrase_part__label;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("<")) == ComparisonEqual) {
	  // PhrasePart name override.

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    part_name = grab (new String (token));

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    if (compareByteArrays (token, ConstMemoryDesc::forString (">")) != ComparisonEqual)
		goto _no_match;

	    continue;
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("/")) == ComparisonEqual) {
	  // Inline accept callback

	    Ref<String> cb_name;

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_match;

	    bool repetition = false;
	    if (compareByteArrays (token, ConstMemoryDesc::forString ("!")) == ComparisonEqual) {
		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;

		repetition = true;
	    }

	    bool universal = false;
	    if (compareByteArrays (token, ConstMemoryDesc::forString ("*")) == ComparisonEqual) {
		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;

		universal = true;
	    }

	    cb_name = grab (new String (token));

	    token = getNonwhspToken (token_stream);
	    if (compareByteArrays (token, ConstMemoryDesc::forString ("/")) != ComparisonEqual)
		goto _no_match;

	    if (universal) {
		Ref<PhrasePart_UniversalAcceptCb> phrase_part__match_cb = grab (new PhrasePart_UniversalAcceptCb);
		phrase_part__match_cb->cb_name = cb_name;
		phrase_part__match_cb->repetition = repetition;
		phrase_part = phrase_part__match_cb;
	    } else {
		Ref<PhrasePart_AcceptCb> phrase_part__match_cb = grab (new PhrasePart_AcceptCb);
		phrase_part__match_cb->cb_name = cb_name;
		phrase_part__match_cb->repetition = repetition;
		phrase_part = phrase_part__match_cb;
	    }
	} else
	if (compareByteArrays (token, ConstMemoryDesc::forString ("[")) == ComparisonEqual) {
	  // Particular token

	    // Concatenated token
	    Ref<String> cat_token;
	    for (;;) {
		token = getNonwhspToken (token_stream);
		if (token.getLength () == 0)
		    goto _no_match;

		// TODO Support for escapement: '\\' stands for '\', '\]' stands for ']'.

		if (compareByteArrays (token, ConstMemoryDesc::forString ("]")) == ComparisonEqual) {
		    for (;;) {
		      // We can easily distinct between a ']' character at the end of the token
		      // and a closing bracket.

			TokenStream::PositionMarker pmark;
			token_stream->getPosition (&pmark);

			token = getNonwhspToken (token_stream);
			if (token.getLength () == 0)
			    break;

			if (compareByteArrays (token, ConstMemoryDesc::forString ("]")) != ComparisonEqual) {
			    token_stream->setPosition (&pmark);
			    break;
			}

			cat_token = String::forPrintTask (Pt (cat_token) (token));
		    }

		    break;
		}

		// Note: This is (very) ineffective for very long tokens.
		cat_token = String::forPrintTask (Pt (cat_token) (token));
	    }

	    if (cat_token.isNull ()) {
		// TODO Parsing error (empty token).
		abortIfReached ();
		goto _no_match;
	    }

	    Ref<PhrasePart_Token> phrase_part__token = grab (new PhrasePart_Token);
	    phrase_part__token->token = cat_token;
	    phrase_part = phrase_part__token;

	    {
		TokenStream::PositionMarker suffix_marker;
		token_stream->getPosition (&suffix_marker);

		token = getNonwhspToken (token_stream);
		if (token.getLength () > 0) {
		    if (compareByteArrays (token, ConstMemoryDesc::forString ("_opt")) == ComparisonEqual)
			phrase_part__token->opt = true;
		    else
			token_stream->setPosition (&suffix_marker);
		}
	    }
	} else {
#if 0
	    // TODO It's better to check here than at the very end of this function.

	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: checking name").pendl ();
	    )

	    {
		Ref<TokenStream::PositionMarker> part_marker = token_stream->getPosition ();

		Ref<String> token = getNextToken (token_stream);
		if (token.getLength () > 0 &&
		    !TokenStream::isNewline (token) &&
		    (compareStrings (token->getData (), ")") ||
		     compareStrings (token->getData (), "|")))
		{
		    goto _no_match;
		}

		token_stream->setPosition (part_marker);
	    }
#endif

	    Ref<PhrasePart_Phrase> phrase_part__phrase = grab (new PhrasePart_Phrase);
	    phrase_part = phrase_part__phrase;

	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: original name: ").print (token).pendl ();
	    )
	    ConstMemoryDesc name;
	    if (stringHasSuffix (token, ConstMemoryDesc::forString ("_opt_seq"), &name) ||
		stringHasSuffix (token, ConstMemoryDesc::forString ("_seq_opt"), &name))
	    {
		DEBUG (
		    errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: _opt_seq").pendl ();
		)
		phrase_part__phrase->opt = true;
		phrase_part__phrase->seq = true;
	    } else
	    if (stringHasSuffix (token, ConstMemoryDesc::forString ("_opt"), &name)) {
		DEBUG (
		    errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: _opt").pendl ();
		)
		phrase_part__phrase->opt = true;
	    } else
	    if (stringHasSuffix (token, ConstMemoryDesc::forString ("_seq"), &name)) {
		DEBUG (
		    errf->print ("Pargen.(PargenTaskParser).parsePhrasePart: _seq").pendl ();
		)
		phrase_part__phrase->seq = true;
	    }

	    if (!part_name.isNull ()) {
		phrase_part->name = part_name;
		phrase_part->name_is_explicit = true;
		part_name = NULL;
	    } else {
		phrase_part->name = capitalizeName (name);

		// Decapitalizing the first letter.
		if (phrase_part->name->getLength () > 0) {
		    char c = phrase_part->name->getData () [0];
		    if (c >= 0x41 /* 'A' */ &&
			c <= 0x5a /* 'Z' */)
		    {
			phrase_part->name->getData () [0] = c + 0x20;
		    }
		}
	    }

	    phrase_part__phrase->phrase_name = capitalizeName (name);
	}

//	errf->print ("P");
	break;
    } // for (;;)

    {
	TokenStream::PositionMarker initial_marker;
	token_stream->getPosition (&initial_marker);

	ConstMemoryDesc token = getNextToken (token_stream);
	if (token.getLength () > 0 &&
	    !TokenStream::isNewline (token))
	{
	    if ((compareByteArrays (token, ConstMemoryDesc::forString (")")) == ComparisonEqual && !vertical_equivalence) ||
		compareByteArrays (token, ConstMemoryDesc::forString ("|")) == ComparisonEqual ||
		compareByteArrays (token, ConstMemoryDesc::forString (":")) == ComparisonEqual ||
		compareByteArrays (token, ConstMemoryDesc::forString ("{")) == ComparisonEqual ||
		compareByteArrays (token, ConstMemoryDesc::forString ("=")) == ComparisonEqual)
	    {
//		errf->print ("E");
		goto _no_match;
	    }
	}

	token_stream->setPosition (&initial_marker);
    }

//    errf->print ("M");
    return phrase_part;
}

_no_match:
    token_stream->setPosition (&marker);
//    errf->print ("N");
    return NULL;
}

static Ref<Declaration_Phrases::PhraseRecord>
parsePhrase (TokenStream *token_stream,
	     LookupData  *lookup_data,
	     bool *ret_null_phrase)
    throw (ParsingException)
{
    abortIf (token_stream == NULL ||
	     lookup_data == NULL);

    if (ret_null_phrase != NULL)
	*ret_null_phrase = true;

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parsePhrase").pendl ();
    )

    TokenStream::PositionMarker marker;
    token_stream->getPosition (&marker);

{

    Ref<Declaration_Phrases::PhraseRecord> phrase_record = grab (new Declaration_Phrases::PhraseRecord);
    phrase_record->phrase = grab (new Phrase);

    // Parsing phrase header. Some valid examples:
    //
    //     \n)		foo)
    //     A|		|)
    //     |||A||)	A|B|||C|
    //     A|B|foo)	\n|
    // 

    bool got_header = false;

    for (;;) {
	TokenStream::PositionMarker name_marker;
	token_stream->getPosition (&name_marker);
	{
	    ConstMemoryDesc token;

	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parsePhrase: iteration").pendl ();
	    )

	    // TODO Can't this be optimized?
	    // Checks in the following loop are pretty much the same.

	    token = getNonwhspToken (token_stream);
	    if (token.getLength () == 0)
		goto _no_name;

	    if (compareByteArrays (token, ConstMemoryDesc::forString (":")) == ComparisonEqual ||
//		compareByteArrays (token, ConstMemoryDesc::forString ("{")) == ComparisonEqual ||
		compareByteArrays (token, ConstMemoryDesc::forString ("=")) == ComparisonEqual)
	    {
		throw ParsingException (token_stream->getFilePosition (),
					String::forPrintTask (Pt ("Unexpected '") (token) ("'")));
	    }

	    if (compareByteArrays (token, ConstMemoryDesc::forString ("[")) == ComparisonEqual)
		goto _no_name;

	    if (compareByteArrays (token, ConstMemoryDesc::forString (")")) == ComparisonEqual) {
	      // Empty name
		got_header = true;

		token_stream->getPosition (&marker);
		name_marker = marker;
		break;
	    }

	    if (compareByteArrays (token, ConstMemoryDesc::forString ("|")) == ComparisonEqual) {
	      // We should get here only for '|' tokens at the very start.

		got_header = true;

		token_stream->getPosition (&marker);
		name_marker = marker;
		continue;
	    }

	    for (;;) {
		Ref<String> name = grab (new String (token));

		token = getNextToken (token_stream);
		if (token.getLength () == 0) {
		    if (!got_header) {
			goto _no_name;
		    } else {
		      // Empty name
			break;
		    }
		}

		if (TokenStream::isNewline (token))
		    goto _no_name;

		if (compareByteArrays (token, ConstMemoryDesc::forString ("|")) == ComparisonEqual) {
		  // Got a variant's name

		    got_header = true;

		    phrase_record->variant_names.append (name);

		    for (;;) {
			token_stream->getPosition (&marker);
			name_marker = marker;

			token = getNonwhspToken (token_stream);
			if (token.getLength () == 0)
			    goto _no_name;

			if (compareByteArrays (token, ConstMemoryDesc::forString (":")) == ComparisonEqual ||
//			    compareByteArrays (token, ConstMemoryDesc::forString ("{")) == ComparisonEqual ||
			    compareByteArrays (token, ConstMemoryDesc::forString ("=")) == ComparisonEqual)
			{
			    throw ParsingException (token_stream->getFilePosition (),
						    String::forData ("Unexpected ':' or '{'"));
			}

			if (compareByteArrays (token, ConstMemoryDesc::forString ("[")) == ComparisonEqual)
			    goto _no_name;

			if (compareByteArrays (token, ConstMemoryDesc::forString (")")) == ComparisonEqual) {
			    token_stream->getPosition (&marker);
			    name_marker = marker;
			    break;
			}

			if (compareByteArrays (token, ConstMemoryDesc::forString ("|")) == ComparisonEqual) {
			    token_stream->getPosition (&marker);
			    name_marker = marker;
			    continue;
			}

			break;
		    }

		    name = grab (new String (token));
		    continue;
		}

		if (compareByteArrays (token, ConstMemoryDesc::forString (")")) == ComparisonEqual) {
		  // We've got phrase's name

		    got_header = true;

		    token_stream->getPosition (&marker);
		    name_marker = marker;

		    phrase_record->phrase->phrase_name = name;
		    break;
		}

		// We've stepped into phrase's real contents. Reverting back.
		goto _no_name;
	    } // for (;;)

	    break;
	}

    _no_name:
	token_stream->setPosition (&name_marker);

	break;
    } // for (;;)

    for (;;) {
	Ref<PhrasePart> phrase_part = parsePhrasePart (token_stream, lookup_data);
	if (phrase_part.isNull ()) {
	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parsePhrase: null phrase part").pendl ();
	    )
	    break;
	}

	if (phrase_record->phrase->phrase_name.isNull ()) {
	  // Auto-generating phrase's name if it is not specified explicitly.

	    // FIXME Sane automatic phrase names
	    switch (phrase_part->phrase_part_type) {
		case PhrasePart::t_Phrase: {
		    PhrasePart_Phrase * const &phrase_part__phrase = static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());
		    abortIf (phrase_part__phrase->name.isNull ());
		    phrase_record->phrase->phrase_name = capitalizeName (phrase_part__phrase->name->getMemoryDesc ());
		} break;
		case PhrasePart::t_Token: {
		    PhrasePart_Token * const &phrase_part__token = static_cast <PhrasePart_Token*> (phrase_part.ptr ());
		    if (phrase_part__token->token.isNull ()) {
			phrase_record->phrase->phrase_name = String::forData ("Any");
		    } else {
			// Note: This is pretty dumb. What if token is not a valid name?
			phrase_record->phrase->phrase_name = capitalizeName (phrase_part__token->token->getMemoryDesc ());
		    }
		} break;
		case PhrasePart::t_AcceptCb: {
		    phrase_record->phrase->phrase_name = String::forData ("AcceptCb");
		} break;
		case PhrasePart::t_UniversalAcceptCb: {
		    phrase_record->phrase->phrase_name = String::forData ("UniversalAcceptCb");
		} break;
		case PhrasePart::t_UpwardsAnchor: {
		    phrase_record->phrase->phrase_name = String::forData ("UpwardsAnchor");
		} break;
		default:
		    abortIfReached ();
	    }
	}

	phrase_record->phrase->phrase_parts.append (phrase_part);
    }

    if (got_header) {
	if (ret_null_phrase != NULL)
	    *ret_null_phrase = false;
    }

    if (phrase_record->phrase->phrase_parts.isEmpty ())
	return NULL;

    if (ret_null_phrase != NULL)
	*ret_null_phrase = false;

    abortIf (phrase_record->phrase->phrase_name.isNull ());
    return phrase_record;
}

#if 0
// Unused
_no_match:
    token_stream->setPosition (&marker);
    return NULL;
#endif
}

static Ref<Declaration_Phrases>
parseDeclaration_Phrases (TokenStream *token_stream,
			  LookupData  *lookup_data)
    throw (ParsingException)
{
    abortIf (token_stream == NULL ||
	     lookup_data == NULL);

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parseDeclaration_Phrases").pendl ();
    )

    Ref<Declaration_Phrases> decl = grab (new Declaration_Phrases);

#if 0
// Note: This might be an alternate mechanism for specifying callbacks
    {
	Ref<TokenStream::PositionMarker> pmark = token_stream->getPosition ();

	Ref<String> token = getNonwhspToken (token_stream);
	if (token.isNull ())
	    return decl;

	if (compareStrings (token->getData (), "$")) {
	    token = getNonwhspToken (token_stream);
	    if (token.isNull ())
		return decl;

	    if (compareStrings (token->getData (), "begin")) {
		bool is_repetition = false;

		token = getNonwhspToken (token_stream);
		if (token.isNull ())
		    throw ParsingException (token_stream->getFilePosition (),
					    String::forData ("Callback name or '!' expected"));

		if (compareStrings (token->getData (), "!")) {
		    is_repetition = true;

		    token = getNonwhspToken (token_stream);
		    if (token.isNull ())
			throw ParsingException (token_stream->getFilePosition (),
						String::forData ("Callback name expected"));
		}

		decl->begin_cb_name = token;
		decl->begin_cb_repetition = is_repetition;
	    } else
		throw ParsingException (token_stream->getFilePosition (),
					String::forData ("Unknown special callback"));
	} else
	    token_stream->setPosition (pmark);
    }
#endif

    for (;;) {
	bool null_phrase;
	Ref<Declaration_Phrases::PhraseRecord> phrase_record = parsePhrase (token_stream, lookup_data, &null_phrase);
	if (null_phrase) {
	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parseDeclaration_Phrases: null phrase").pendl ();
	    )
	    break;
	}

	if (!phrase_record.isNull ()) {
	    DEBUG (
		errf->print ("Pargen.(PargenTaskParser).parseDeclaration_Phrases: "
			     "appending phrase: ").print (phrase_record->phrase->phrase_name).pendl ();
	    )
	    decl->phrases.append (phrase_record);
	}
    }

    return decl;
}

static Ref<Declaration_Callbacks>
parseDeclaration_Callbacks (TokenStream *token_stream)
    throw (ParsingException)
{
    abortIf (token_stream == NULL);

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parseDeclaration_Callbacks").pendl ();
    )

    Ref<Declaration_Callbacks> decl = grab (new Declaration_Callbacks);

    for (;;) {
	FilePosition fpos = token_stream->getFilePosition ();
	ConstMemoryDesc token = getNonwhspToken (token_stream);
	if (token.getLength () == 0)
	    throw ParsingException (fpos,
				    String::forData ("'}' expected"));

	if (compareByteArrays (token, ConstMemoryDesc::forString ("}")) == ComparisonEqual)
	    break;

	Ref<CallbackDecl> callback_decl = grab (new CallbackDecl);
	callback_decl->callback_name = grab (new String (token));
	decl->callbacks.append (callback_decl);
    }

    return decl;
}

static Ref<Declaration_Phrases>
parseDeclaration_Alias (TokenStream *token_stream)
    throw (ParsingException)
{
    abortIf (token_stream == NULL);

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parseDeclaration_Alias").pendl ();
    )

    Ref<Declaration_Phrases> decl = grab (new Declaration_Phrases);
    decl->is_alias = true;

    ConstMemoryDesc token = getNonwhspToken (token_stream);
    if (token.getLength () == 0)
	throw ParsingException (token_stream->getFilePosition (),
				String::forData ("alias excpected"));

    decl->aliased_name = capitalizeName (token);

    return decl;
}

static Ref<Declaration>
parseDeclaration (TokenStream *token_stream,
		  LookupData  *lookup_data)
    throw (ParsingException)
{
    abortIf (token_stream == NULL ||
	     lookup_data == NULL);

    DEBUG (
	errf->print ("Pargen.(PargenTaskParser).parseDeclaration").pendl ();
    )

    TokenStream::PositionMarker marker;
    token_stream->getPosition (&marker);

{
    FilePosition fpos = token_stream->getFilePosition ();
    ConstMemoryDesc token = getNonwhspToken (token_stream);
    if (token.getLength () == 0)
	goto _no_match;

    Ref<String> declaration_name = capitalizeName (token);
    Ref<String> lowercase_declaration_name = lowercaseName (token);

    fpos = token_stream->getFilePosition ();
    token = getNonwhspToken (token_stream);
    if (token.getLength () == 0)
	throw ParsingException (fpos,
				String::forData ("':' or '{' expected"));

    Ref<Declaration> decl;
    if (compareByteArrays (token, ConstMemoryDesc::forString (":")) == ComparisonEqual) {
	Ref<Declaration_Phrases> decl_phrases = parseDeclaration_Phrases (token_stream, lookup_data);
	decl = decl_phrases;

	DEBUG (
	    errf->print ("Pargen.(PargenTaskParser).parseDeclaration: "
			 "adding Declaration_Phrases: ").print (declaration_name).pendl ();
	)
	if (!lookup_data->addDeclaration (decl_phrases, declaration_name->getMemoryDesc ()))
	    errf->print ("Pargen.(PargenTaskParser).parseDeclaration: "
			 "duplicate Declaration_Phrases name: ").print (declaration_name).pendl ();
    } else
    if (compareByteArrays (token, ConstMemoryDesc::forString ("{")) == ComparisonEqual) {
	decl = parseDeclaration_Callbacks (token_stream);
    } else
    if (compareByteArrays (token, ConstMemoryDesc::forString ("=")) == ComparisonEqual) {
	Ref<Declaration_Phrases> decl_phrases = parseDeclaration_Alias (token_stream);
	decl = decl_phrases;

	if (!lookup_data->addDeclaration (decl_phrases, declaration_name->getMemoryDesc ()))
		errf->print ("Pargen.(PargenTaskParser).parseDeclaration: "
			     "duplicate _Alias name: ").print (declaration_name).pendl ();
    } else {
	throw ParsingException (fpos,
				String::forData ("':' or '{' expected"));
    }

    abortIf (decl.isNull ());

    decl->declaration_name = declaration_name;
    decl->lowercase_declaration_name = lowercase_declaration_name;

    return decl;
}

_no_match:
    token_stream->setPosition (&marker);
    return NULL;
}

static bool
linkPhrases (Declaration_Phrases *decl_phrases,
	     LookupData  *lookup_data)
    throw (ParsingException)
{
    abortIf (decl_phrases == NULL ||
	     lookup_data == NULL);

    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
    while (!phrase_iter.done ()) {
	Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
	abortIf (phrase_record.isNull ());
	Phrase *phrase = phrase_record->phrase;
	List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase->phrase_parts);
	while (!phrase_part_iter.done ()) {
	    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();
	    if (phrase_part->phrase_part_type == PhrasePart::t_Phrase) {
		PhrasePart_Phrase * const &phrase_part__phrase = static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());
		abortIf (phrase_part__phrase->phrase_name.isNull ());
		if (phrase_part__phrase->decl_phrases == NULL) {
		    Bool is_alias;
		    Ref<Declaration_Phrases> decl_phrases =
			    lookup_data->lookupDeclaration (phrase_part__phrase->phrase_name->getMemoryDesc (), &is_alias);
		    if (decl_phrases.isNull ()) {
			// TODO throw ParsingException
			errf->print ("Pargen.(PargenTaskParser).linkPhrases: "
				     "unresolved name: ").print (phrase_part__phrase->phrase_name).pendl ();
			return false;
		    }

		    phrase_part__phrase->decl_phrases = decl_phrases;

		    if (is_alias &&
			!phrase_part->name_is_explicit)
		    {
			phrase_part->name = String::forData (decl_phrases->declaration_name->getData ());

			// Decapitalizing the first letter.
			if (phrase_part->name->getLength () > 0) {
			    char c = phrase_part->name->getData () [0];
			    if (c >= 0x41 /* 'A' */ &&
				c <= 0x5a /* 'Z' */)
			    {
				phrase_part->name->getData () [0] = c + 0x20;
			    }
			}
		    }
		}
	    }
	}
    }

    return true;
}

typedef Map < Ref<Declaration_Callbacks>,
	      DereferenceExtractor< Declaration_Callbacks,
				    MemoryDesc,
				    MemberExtractor< Declaration,
						     Ref<String>,
						     &Declaration::declaration_name,
						     MemoryDesc,
						     AccessorExtractor< String,
									MemoryDesc,
									&String::getMemoryDesc > > >,
	      MemoryComparator<> >
	Map__Declaration_Callbacks;

static void
linkCallbacks (Declaration_Phrases *decl_phrases,
	       Map__Declaration_Callbacks &decl_callbacks)
{
    abortIf (decl_phrases == NULL);

    abortIf (decl_phrases->declaration_name.isNull ());
    MapBase< Ref<Declaration_Callbacks> >::Entry cb_entry =
	    decl_callbacks.lookup (decl_phrases->declaration_name->getMemoryDesc ());
    if (!cb_entry.isNull ()) {
	Ref<Declaration_Callbacks> &decl_callbacks = cb_entry.getData ();
	abortIf (decl_callbacks.isNull ());

	List< Ref<CallbackDecl> >::DataIterator cb_iter (decl_callbacks->callbacks);
	while (!cb_iter.done ()) {
	    Ref<CallbackDecl> &callback_decl = cb_iter.next ();
	    abortIf (callback_decl.isNull () ||
		     callback_decl->callback_name.isNull ());
	    decl_phrases->callbacks.add (callback_decl);
	}
    }
}

static bool
linkAliases (PargenTask * const pargen_task,
	     LookupData * const lookup_data)
{
    List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
    while (!decl_iter.done ()) {
	Ref<Declaration> &decl = decl_iter.next ();
	if (decl->declaration_type != Declaration::t_Phrases)
	    continue;

	Declaration_Phrases &decl_phrases = static_cast <Declaration_Phrases&> (*decl);
	if (!decl_phrases.is_alias)
	    continue;

	Ref<Declaration_Phrases> const aliased_decl =
		lookup_data->lookupDeclaration (decl_phrases.aliased_name->getMemoryDesc (),
						NULL /* ret_is_alias */);
	if (aliased_decl.isNull ()) {
	    errf->print ("Pargen.(PargenTaskParser).linkAliases: unresolved name: ")
		 .print (decl_phrases.aliased_name).pendl ();
	    return false;
	}

	decl_phrases.deep_aliased_name = aliased_decl->declaration_name;

	{
	    MapBase< Ref<LookupData::LookupNode> >::Entry map_entry =
		    lookup_data->lookup_map.lookup (decl_phrases.aliased_name->getMemoryDesc ());
	    if (map_entry.isNull ()) {
		errf->print ("Pargen.(PargenTaskParser).linkAliases: unresolved name: ")
		     .print (decl_phrases.aliased_name).pendl ();
		return false;
	    }

	    abortIf (map_entry.getData ()->decl_phrases.isNull ());
	    decl_phrases.aliased_decl = map_entry.getData ()->decl_phrases;
	}
    }

    return true;
}

#if 0
// Old linking code for upwards anchors.

namespace {

    static void
    print_whsp (File *file,
		Size num_spaces)
    {
	for (Size i = 0; i < (num_spaces << 1); i++)
	    file->print (" ");
    }

    class LinkUpwardsAnchors_Step : public SimplyReferenced
    {
    public:
	Declaration_Phrases *decl_phrases;

	Bool first_phrase;

//	Bool alias_processed;

	Size num_aliases;

	List< Ref<Declaration_Phrases::PhraseRecord> >::Element *cur_phrase_el;
        List< Ref<PhrasePart> >::Element *cur_part_el;

	Size cur_phrase_index;
	Size cur_part_index;

	LinkUpwardsAnchors_Step ()
	    : decl_phrases (NULL),
	      num_aliases (NULL),
	      cur_phrase_el (NULL),
	      cur_part_el (NULL),
	      cur_phrase_index (0),
	      cur_part_index (0)
	{
	}
    };

    class LinkUpwardsAnchors_State
    {
    public:
	List< Ref<LinkUpwardsAnchors_Step> > steps;
    };

    static void
    linkUpwardsAnchors_linkAnchor (LinkUpwardsAnchors_State * const orig_state /* non-null */,
				   PhrasePart_UpwardsAnchor * const src_anchor /* non-null */,
				   Size                       const loop_id,
				   Size                             depth)
    {
      // TODO Карты переходов нужно строить только для корневой грамматики.

#if 0
	if (!src_anchor->jump_path.isNull () &&
	    src_anchor->jump_path->jumps.isEmpty ())
#endif
	if (src_anchor->got_jump_path) {
	    DEBUG_ANCHORS (
		print_whsp (errf, depth);
		errf->print ("LINKED\n");
	    )
	    return;
	}

//	Ref<JumpPath> const jump_path = grab (new JumpPath);
//	src_anchor->jump_path = jump_path;

	LinkUpwardsAnchors_State state;

	{
	  // Duplicating original tree traversal state

	    List< Ref<LinkUpwardsAnchors_Step> >::InverseDataIterator step_iter (orig_state->steps);
//	    Bool first = true;
	    while (!step_iter.done ()) {
		Ref<LinkUpwardsAnchors_Step> &step = step_iter.next ();

#if 0
		if (first) {
		  // The first step corresponds to the current Declaration_Phrases.
		    first = false;
		    continue;
		}
#endif

		step->decl_phrases->loop_id = loop_id;

		Ref<LinkUpwardsAnchors_Step> new_step = grab (new LinkUpwardsAnchors_Step);
		new_step->decl_phrases = step->decl_phrases;
		new_step->first_phrase = true;
		new_step->num_aliases = step->num_aliases;
		new_step->cur_phrase_el = step->cur_phrase_el;
		new_step->cur_part_el = step->cur_part_el;
		new_step->cur_phrase_index = step->cur_phrase_index;
		new_step->cur_part_index = step->cur_part_index;

		state.steps.prepend (new_step);
	    }
	}

	Declaration_Phrases *top_decl = NULL;

	Size rollback_depth = 0;

#if 0
	DEBUG_ANCHORS (
	    print_whsp (errf, depth);
	    errf->print ("linkUpwardsAnchors\n");
	)
#endif

//	bool first_phrase = true;
	while (!state.steps.isEmpty ()) {
	    LinkUpwardsAnchors_Step * const cur_step = state.steps.last->data;
	    Declaration_Phrases * const decl_phrases = cur_step->decl_phrases;

	    if (rollback_depth == 0) {
		top_decl = decl_phrases;

		DEBUG_ANCHORS (
		    print_whsp (errf, depth);
		    errf->print ("L top ").print (decl_phrases->declaration_name).print ("\n");
		)
	    }

	    DEBUG_ANCHORS (
		print_whsp (errf, depth);
		errf->print ("L decl_phrases ").print (decl_phrases->declaration_name).print ("\n");
	    )

	    if (cur_step->cur_phrase_el != NULL) {
		Ref<Declaration_Phrases::PhraseRecord> &phrase_record = cur_step->cur_phrase_el->data;

		DEBUG_ANCHORS (
		    print_whsp (errf, depth);
		    errf->print ("L record ").print (phrase_record->phrase->phrase_name).print ("\n");
		)

		depth ++;

		bool break_part_loop = false;
#if 0
// Deprecated
		List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase_record->phrase->phrase_parts);
		Size part_index = 0;
		while (!phrase_part_iter.done ()) {
		    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();
#endif
		while (!cur_step->first_phrase &&
		       cur_step->cur_part_el != NULL)
		{
		    Ref<PhrasePart> &phrase_part = cur_step->cur_part_el->data;

		    DEBUG_ANCHORS (
			print_whsp (errf, depth);
			errf->print ("L part ").print (phrase_part->toString ()).print ("\n");
		    )

		    depth ++;

		    switch (phrase_part->phrase_part_type) {
			case PhrasePart::t_Phrase: {
			    PhrasePart_Phrase * const phrase_part__phrase =
				    static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

			    DEBUG_ANCHORS (
				print_whsp (errf, depth);
				errf->print ("L phrase ").print (phrase_part__phrase->decl_phrases->declaration_name).print ("\n");
			    )

			    Declaration_Phrases *next_decl_phrases = phrase_part__phrase->decl_phrases;
			    Size num_aliases = 0;
			    Bool skip = false;
			    for (;;) {
				if (next_decl_phrases->loop_id == loop_id) {
				    skip = true;
				    break;
				}
				next_decl_phrases->loop_id = loop_id;

				if (next_decl_phrases->is_alias) {
				    num_aliases ++;

				    abortIf (next_decl_phrases->aliased_decl == NULL);
				    next_decl_phrases = next_decl_phrases->aliased_decl;
				    continue;
				}

				break;
			    }

			    if (!skip) {
#if 0
// Deprecated
			    if (phrase_part__phrase->decl_phrases->loop_id != loop_id) {
				phrase_part__phrase->decl_phrases->loop_id = loop_id;
#endif

				Ref<LinkUpwardsAnchors_Step> step = grab (new LinkUpwardsAnchors_Step);
				step->decl_phrases = next_decl_phrases;
				step->num_aliases = num_aliases;
				step->cur_phrase_el = next_decl_phrases->phrases.first;
				step->cur_part_el = next_decl_phrases->phrases.first->data->phrase->phrase_parts.first;
				step->cur_phrase_index = 0;
				step->cur_part_index = 0;

				state.steps.append (step);

#if 0
				{
				    Ref<Jump_Push> const jump_push = grab (new Jump_Push);
				    jump_push->decl_phrases = next_decl_phrases;
				    jump_push->phrase_el = cur_step->cur_phrase_el;
				    jump_push->switch_index = cur_step->cur_phrase_index;
				    jump_push->compound_index = cur_step->cur_part_index;

				    jump_path->jumps.append (jump_push);
				}
				jump_path->rollback_depth ++;
#endif
				rollback_depth ++;

				depth += 3;

				break_part_loop = true;
			    } else {
				DEBUG_ANCHORS (
				    print_whsp (errf, depth);
				    errf->print ("L skipped").print ("\n");
				)
			    }
			} break;
			case PhrasePart::t_UpwardsAnchor: {
			    PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor =
				    static_cast <PhrasePart_UpwardsAnchor*> (phrase_part.ptr ());

			    DEBUG_ANCHORS(
				print_whsp (errf, depth);
				errf->print ("L anchor ").print (phrase_part__upwards_anchor->anchor_name);
			    )

			    if (compareByteArrays (phrase_part__upwards_anchor->anchor_name->getMemoryDesc (),
						   src_anchor->anchor_name->getMemoryDesc ())
					== ComparisonEqual)
			    {
				src_anchor->jump_path.dest_decl = decl_phrases;
				src_anchor->jump_path.top_decl = top_decl;
				src_anchor->jump_path.switch_index = cur_step->cur_phrase_index;
				src_anchor->jump_path.compound_index = cur_step->cur_part_index;
				src_anchor->got_jump_path = 1;

				DEBUG_ANCHORS (
				    errf->print (" MATCH\n");

				    print_whsp (errf, depth);
				    errf->print ("L dest: ").print (src_anchor->jump_path.dest_decl->declaration_name).print (", "
						 "top: ").print (src_anchor->jump_path.top_decl->declaration_name).print (", "
						 "switch_i: ").print (src_anchor->jump_path.switch_index).print (", "
						 "compound_i: ").print (src_anchor->jump_path.compound_index).print ("\n");
				)

				return;
			    }

			    DEBUG_ANCHORS (
				errf->print ("\n");
			    )
			} break;
			default:
			  // Nothing to do
			    break;
		    }

		    depth --;

		    cur_step->cur_part_el = cur_step->cur_part_el->next;
		    cur_step->cur_part_index ++;

		    if (break_part_loop) {
			DEBUG_ANCHORS(
			    print_whsp (errf, depth - 1);
			    errf->print ("->\n");
			)

			break;
		    }

#if 0
// Deprecated
		    part_index ++;
#endif
		}

		depth --;

		if (cur_step->first_phrase ||
		    cur_step->cur_part_el == NULL)
		{
		    List< Ref<Declaration_Phrases::PhraseRecord> >::Element * const old_phrase_el = cur_step->cur_phrase_el;

		    cur_step->cur_phrase_el = old_phrase_el->next;

		    if (old_phrase_el->next != NULL)
			cur_step->cur_part_el = old_phrase_el->next->data->phrase->phrase_parts.first;
		    else
			cur_step->cur_part_el = NULL;

		    cur_step->cur_phrase_index ++;
		    cur_step->cur_part_index = 0;
		}

		cur_step->first_phrase = false;
	    } else {
		state.steps.remove (state.steps.last);

#if 0
		if (!jump_path->jumps.isEmpty () &&
		    jump_path->jumps.last->data->getType () == Jump::t_Push)
		{
		    jump_path->jumps.remove (jump_path->jumps.last);
		} else {
		    Ref<Jump_Pop> const jump_pop = grab (new Jump_Pop);
		    jump_path->jumps.append (jump_pop);
		}

		jump_path->rollback_depth --;
#endif
		rollback_depth --;

		depth -= 3;
	    }
	}
    }

}

static void
linkUpwardsAnchors (PargenTask * const pargen_task)
{
  // Walking through the whole grammar tree and calling _linkAnchor()
  // for each upwards anchor.

    LinkUpwardsAnchors_State state;

    Size depth = 0;

    Size loop_id = 1;
    Size const decl_loop_id = 1;
    List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
    while (!decl_iter.done ()) {
	Ref<Declaration> &decl = decl_iter.next ();
	if (decl->declaration_type != Declaration::t_Phrases)
	    continue;

	DEBUG_ANCHORS(
	    print_whsp (errf, depth);
	    errf->print ("decl ").print (decl->declaration_name).print ("\n");
	)

	Declaration_Phrases *top_decl_phrases = static_cast <Declaration_Phrases*> (decl.ptr ());
	Size top_num_aliases = 0;
	{
	    Bool top_skip = false;
	    for (;;) {
		if (top_decl_phrases->decl_loop_id == decl_loop_id) {
		    DEBUG_ANCHORS(
			print_whsp (errf, depth);
			errf->print ("skipped decl\n");
		    )

		    top_skip = true;
		    break;
		}
		top_decl_phrases->decl_loop_id = decl_loop_id;

		if (top_decl_phrases->is_alias) {
		    top_num_aliases ++;

		    abortIf (top_decl_phrases->aliased_decl == NULL);
		    top_decl_phrases = top_decl_phrases->aliased_decl;
		    continue;
		}

		break;
	    }

	    if (top_skip)
		continue;
	}

	Ref<LinkUpwardsAnchors_Step> top_step = grab (new LinkUpwardsAnchors_Step);
	top_step->decl_phrases = top_decl_phrases;
	top_step->num_aliases = top_num_aliases;
	top_step->cur_phrase_el = top_decl_phrases->phrases.first;
	top_step->cur_part_el = top_decl_phrases->phrases.first->data->phrase->phrase_parts.first;
	top_step->cur_phrase_index = 0;
	top_step->cur_part_index = 0;

	state.steps.append (top_step);

	depth += 3;

	while (!state.steps.isEmpty ()) {
	    LinkUpwardsAnchors_Step * const cur_step = state.steps.last->data;
	    Declaration_Phrases * const decl_phrases = cur_step->decl_phrases;

	    DEBUG_ANCHORS(
		print_whsp (errf, depth);
		errf->print ("decl_phrases ").print (decl_phrases->declaration_name).print (
			     " 0x").printHex ((Uint64) cur_step).print (" : ").printHex ((Uint64) cur_step->cur_phrase_el).print ("\n");
	    )

	    if (cur_step->cur_phrase_el != NULL) {
		Ref<Declaration_Phrases::PhraseRecord> &phrase_record = cur_step->cur_phrase_el->data;

		DEBUG_ANCHORS (
		    print_whsp (errf, depth);
		    errf->print ("record ").print (phrase_record->phrase->phrase_name).print ("\n");
		)

		depth ++;

#if 0
// Deprecated
		List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase_record->phrase->phrase_parts);
		while (!phrase_part_iter.done ()) {
		    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();
#endif
		while (cur_step->cur_part_el != NULL) {
		    Ref<PhrasePart> &phrase_part = cur_step->cur_part_el->data;

		    DEBUG_ANCHORS (
			print_whsp (errf, depth);
			errf->print ("part ").print (phrase_part->toString ()).print ("\n");
		    )

		    depth ++;

		    bool break_part_loop = false;
		    switch (phrase_part->phrase_part_type) {
			case PhrasePart::t_Phrase: {
			    PhrasePart_Phrase * const phrase_part__phrase =
				    static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

			    DEBUG_ANCHORS (
				print_whsp (errf, depth);
				errf->print ("subphrase ").print (phrase_part__phrase->decl_phrases->declaration_name).print ("\n");
			    )

			    Declaration_Phrases *next_decl_phrases = phrase_part__phrase->decl_phrases;
			    Size num_aliases = 0;
			    Bool skip = false;
			    for (;;) {
				if (next_decl_phrases->decl_loop_id == decl_loop_id) {
				    skip = true;
				    break;
				}
				next_decl_phrases->decl_loop_id = decl_loop_id;

				if (next_decl_phrases->is_alias) {
				    num_aliases ++;

				    abortIf (next_decl_phrases->aliased_decl == NULL);
				    next_decl_phrases = next_decl_phrases->aliased_decl;
				    continue;
				}

				break;
			    }

			    if (!skip) {
				Ref<LinkUpwardsAnchors_Step> step = grab (new LinkUpwardsAnchors_Step);
				step->decl_phrases = next_decl_phrases;
				step->num_aliases = num_aliases;
				step->cur_phrase_el = next_decl_phrases->phrases.first;
				step->cur_part_el = next_decl_phrases->phrases.first->data->phrase->phrase_parts.first;
				step->cur_phrase_index = 0;
				step->cur_part_index = 0;

				state.steps.append (step);

				depth += 3;

				break_part_loop = true;
			    } else {
				DEBUG_ANCHORS (
				    print_whsp (errf, depth);
				    errf->print ("skipped").print ("\n");
				)
			    }
			} break;
			case PhrasePart::t_UpwardsAnchor: {
			    PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor =
				    static_cast <PhrasePart_UpwardsAnchor*> (phrase_part.ptr ());

			    DEBUG_ANCHORS(
				print_whsp (errf, depth);
				errf->print ("anchor ").print (phrase_part__upwards_anchor->anchor_name).print ("\n");
			    )

			    linkUpwardsAnchors_linkAnchor (&state,
							   phrase_part__upwards_anchor,
							   loop_id,
							   depth + 1);

			    DEBUG_ANCHORS(
				print_whsp (errf, depth);
				errf->print ("DONE\n");
			    )

			    loop_id ++;
			} break;
			default:
			  // Nothing to do
			    break;
		    }

		    depth --;

		    cur_step->cur_part_el = cur_step->cur_part_el->next;
		    cur_step->cur_part_index ++;

		    if (break_part_loop) {
			DEBUG_ANCHORS(
			    print_whsp (errf, depth - 1);
			    errf->print ("->\n");
			)

			break;
		    }
		}

		depth --;

		if (cur_step->cur_part_el == NULL) {
		    List< Ref<Declaration_Phrases::PhraseRecord> >::Element * const old_phrase_el = cur_step->cur_phrase_el;

		    cur_step->cur_phrase_el = old_phrase_el->next;

		    if (old_phrase_el->next != NULL)
			cur_step->cur_part_el = old_phrase_el->next->data->phrase->phrase_parts.first;
		    else
			cur_step->cur_part_el = NULL;

		    cur_step->cur_phrase_index ++;
		    cur_step->cur_part_index = 0;
		}
	    } else {
#if 0
		DEBUG_ANCHORS(
		    print_whsp (errf, depth);
		    errf->print ("remove\n");
		)
#endif

		state.steps.remove (state.steps.last);

		depth -= 3;
	    }
	}

// Unnecessary	decl_loop_id ++;
    }
    abortIf (!state.steps.isEmpty ());
}
#endif

static bool
linkUpwardsAnchors_linkAnchor (LookupData               * const lookup_data,
			       PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor)
{
    static char const * const _func_name = "Pargen.PargenTaskParser.linkUpwardsAnchors_linkAnchor";

    Ref<Declaration_Phrases> decl_phrases =
	    lookup_data->lookupDeclaration (
		    phrase_part__upwards_anchor->declaration_name->getMemoryDesc (),
		    NULL /* ret_is_alias */);
    if (decl_phrases.isNull ()) {
	errf->print (_func_name).print (": unresolved name: ")
		     .print (phrase_part__upwards_anchor->declaration_name).pendl ();
	return false;
    }

    bool got_switch_grammar_index = false;
    Size switch_grammar_index = 0;
    bool got_compound_grammar_index = false;
    Size compound_grammar_index = 0;
    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
    while (!phrase_iter.done ()) {
	Ref<Declaration_Phrases::PhraseRecord> phrase_record = phrase_iter.next ();

	if (compareByteArrays (phrase_record->phrase->phrase_name->getMemoryDesc (),
			       phrase_part__upwards_anchor->phrase_name->getMemoryDesc ())
		    == ComparisonEqual)
	{
	    got_switch_grammar_index = true;

	    List< Ref<PhrasePart> >::DataIterator part_iter (phrase_record->phrase->phrase_parts);
	    bool got_jump = false;
	    while (!part_iter.done ()) {
		Ref<PhrasePart> &phrase_part = part_iter.next ();

		errf->print ("    ").print (phrase_part->toString ()).pendl ();

		switch (phrase_part->phrase_part_type) {
		    case PhrasePart::t_Label: {
			PhrasePart_Label * const phrase_part__label =
				static_cast <PhrasePart_Label*> (phrase_part.ptr ());

			if (compareByteArrays (phrase_part__label->label_name->getMemoryDesc (),
					       phrase_part__upwards_anchor->label_name->getMemoryDesc ())
				    == ComparisonEqual)
			{
			    errf->print ("    MATCH ").print (phrase_part__label->label_name).pendl ();
			    got_compound_grammar_index = true;
			    got_jump = true;
			}
		    } break;
		    default: {
		      // Note that labels are not counted, because they do not
		      // appear in resulting compound grammars.
			compound_grammar_index ++;
		    } break;
		}

		if (got_jump)
		    break;
	    }

	    break;
	}

	switch_grammar_index ++;
    }

    if (!got_switch_grammar_index) {
	errf->print (_func_name).print (": switch subgrammar not found: ")
		     .print (phrase_part__upwards_anchor->declaration_name).print (":")
		     .print (phrase_part__upwards_anchor->phrase_name).pendl ();
	return false;
    }

    if (!got_compound_grammar_index) {
	errf->print (_func_name).print (": switch label not found: ")
		     .print (phrase_part__upwards_anchor->declaration_name).print (":")
		     .print (phrase_part__upwards_anchor->phrase_name).print ("@")
		     .print (phrase_part__upwards_anchor->label_name).pendl ();
	return false;
    }

    DEBUG_ANCHORS (
	errf->print ("jump: ")
    		     .print (phrase_part__upwards_anchor->declaration_name).print (":")
		     .print (phrase_part__upwards_anchor->phrase_name).print ("@")
		     .print (phrase_part__upwards_anchor->label_name).print (" "
		     "switch_i: ").print (switch_grammar_index).print (", "
		     "compound_i: ").print (compound_grammar_index).pendl ();
    )

    phrase_part__upwards_anchor->switch_grammar_index = switch_grammar_index;
    phrase_part__upwards_anchor->compound_grammar_index = compound_grammar_index;

    return true;
}

static void
linkUpwardsAnchors (PargenTask * const pargen_task,
		    LookupData * const lookup_data)
    throw (ParsingException)
{
    List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
    while (!decl_iter.done ()) {
	Ref<Declaration> &decl = decl_iter.next ();
	if (decl->declaration_type != Declaration::t_Phrases)
	    continue;

	Declaration_Phrases * const decl_phrases = static_cast <Declaration_Phrases*> (decl.ptr ());

	List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
	while (!phrase_iter.done ()) {
	    Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();

	    List< Ref<PhrasePart> >::DataIterator part_iter (phrase_record->phrase->phrase_parts);
	    while (!part_iter.done ()) {
		Ref<PhrasePart> &phrase_part = part_iter.next ();

		switch (phrase_part->phrase_part_type) {
		    case PhrasePart::t_UpwardsAnchor: {
			PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor =
				static_cast <PhrasePart_UpwardsAnchor*> (phrase_part.ptr ());

			if (!linkUpwardsAnchors_linkAnchor (lookup_data,
							    phrase_part__upwards_anchor))
			{
			    // TODO FilePosition
			    throw ParsingException (FilePosition ());
			}
		    } break;
		    default:
		      // Nothing to do
			;
		}
	    }
	}
    }
}

Ref<PargenTask>
parsePargenTask (TokenStream *token_stream)
    throw (ParsingException,
	   InternalException)
{
    abortIf (token_stream == NULL);

    Ref<PargenTask> pargen_task = grab (new PargenTask ());

    Map__Declaration_Callbacks decls_callbacks;

    Ref<LookupData> lookup_data = grab (new LookupData);
    for (;;) {
	Ref<Declaration> decl = parseDeclaration (token_stream, lookup_data);
	if (decl.isNull ())
	    break;

	switch (decl->declaration_type) {
	    case Declaration::t_Phrases: {
		Declaration_Phrases * const &decl_phrases = static_cast <Declaration_Phrases*> (decl.ptr ());
		pargen_task->decls.append (decl_phrases);
	    } break;
	    case Declaration::t_Callbacks: {
		Declaration_Callbacks * const &decl_callbacks = static_cast <Declaration_Callbacks*> (decl.ptr ());
		decls_callbacks.add (decl_callbacks);
	    } break;
#if 0
	    case Declaration::t_Alias: {
		Declaration_Alias * const decl_alias = static_cast <Declaration_Alias*> (decl.ptr ());
		pargen_task->decls.append (decl_alias);
	    } break;
#endif
	    default:
		abortIfReached ();
	}
    }

    if (!linkAliases (pargen_task, lookup_data))
	throw ParsingException (FilePosition ());

    List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
    while (!decl_iter.done ()) {
	Ref<Declaration> &decl = decl_iter.next ();
	if (decl->declaration_type != Declaration::t_Phrases)
	    continue;

	Declaration_Phrases &decl_phrases = static_cast <Declaration_Phrases&> (*decl);

	if (!linkPhrases (&decl_phrases, lookup_data))
	    // TODO File position
	    throw ParsingException (FilePosition ());

	linkCallbacks (&decl_phrases, decls_callbacks);
    }

    linkUpwardsAnchors (pargen_task, lookup_data);

    return pargen_task;
}

void
dumpDeclarations (PargenTask const *pargen_task)
{
    abortIf (pargen_task == NULL);

    errf->print ("Pargen.(PargenTaskParser).dumpDeclarations").pendl ();

    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	while (!decl_iter.done ()) {
	    Ref<Declaration> &decl = decl_iter.next ();
	    abortIf (decl.isNull ());
	    errf->print ("Declaration: ").print (decl->declaration_name).print ("\n");
	}
    }

    errf->pflush ();
}

}


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


#ifndef __PARGEN__GRAMMAR_H__
#define __PARGEN__GRAMMAR_H__

#include <libmary/hash.h>

#include <mycpp/mycpp.h>
#include <mycpp/io.h>

#include <pargen/parser_element.h>
#include <pargen/acceptor.h>

#include <string>
// NO BOOST #include <boost/unordered_set.hpp>


#define DEBUG(a) ;


namespace Pargen {

using namespace MyCpp;

class Parser;
class ParserControl;

/*c
 * Internal representation for grammars.
 *
 * <c>Grammar</c> objects are opaque for pargen users.
 */
class Grammar : public SimplyReferenced,
		public UidProvider
{
public:
    typedef void (*BeginFunc) (void *data);

    // TODO AcceptFunc and MatchFunc should be merged into
    // callbacks identical to InlineMatchFunc.
    // There'll be only one type of callback - "MatchFunc".
    typedef void (*AcceptFunc) (ParserElement *parser_element,
				ParserControl *parser_control,
				void          *data);

    typedef bool (*MatchFunc) (ParserElement *parser_element,
			       ParserControl *parser_control,
			       void          *data);

    typedef bool (*InlineMatchFunc) (ParserElement *parser_element,
				     ParserControl *parser_control,
				     void          *data);

    typedef bool (*JumpFunc) (ParserElement *parser_element,
			      void          *data);

    enum Type {
	t_Immediate,
	t_Compound,
	t_Switch,
	t_Alias
    };

    const Type grammar_type;

    BeginFunc begin_func;

    // Called after the grammar was found to be matching
    // according to the syntax. Returns true if the element is
    // found to be a mathcing one according to lookup data etc.
    MatchFunc match_func;

    // Called after the grammar was found to be the matching one,
    // and chosen as the preferred one among the alternatives.
    AcceptFunc accept_func;

    // Used for detecting infinite grammar loops when optimizing.
    Size loop_id;

    Bool optimized;

    // Returns string representation of the grammar for debugging output.
    virtual Ref<String> toString () = 0;

    Grammar (Type type)
	: grammar_type (type)
    {
	begin_func = NULL;
	match_func = NULL;
	accept_func = NULL;

	loop_id = 0;
    }
};

// FIXME Grammar_Immediate_SingleToken should become Grammar_Immediate.
//       Currently, this is assumed when we do optimizations.
class Grammar_Immediate : public Grammar
{
public:
#if 0
// Unused

    typedef Ref<ParserElement> (*ElementCreationFunc) (const char *token);

    ElementCreationFunc elem_creation_func;
#endif

    // Do not confuse this with match_func().
    // match_func() is supposed to be provided by the user of the grammar.
    // match() is a description of the grammar type, it's an internal
    // pargen mechanism.
    virtual bool match (ConstMemoryDesc const &token,
			void                  *token_user_ptr,
			void                  *user_data) = 0;

    Grammar_Immediate ()
	: Grammar (Grammar::t_Immediate)
    {
#if 0
	elem_creation_func = NULL;
#endif
    }
};

class Grammar_Immediate_SingleToken : public Grammar_Immediate
{
public:
    typedef bool (*TokenMatchCallback) (ConstMemoryDesc const &token,
					void                  *token_user_ptr,
					void                  *user_data);

    TokenMatchCallback token_match_cb;
    // For debug dumps.
    Ref<String> token_match_cb_name;

protected:
    // If null, then any token matches.
    Ref<String> token;

public:
    Ref<String> getToken ()
    {
	return token;
    }

    Ref<String> toString ();

    bool match (ConstMemoryDesc const &t,
		void                  * const token_user_ptr,
		void                  * const user_data)
    {
	if (token_match_cb != NULL)
	    return token_match_cb (t, token_user_ptr, user_data);

	if (token.isNull () ||
	    token->getLength () == 0)
	{
	    return true;
	}

	return compareByteArrays (token->getMemoryDesc (), t) == ComparisonEqual;
    }

    // If token is NULL, then any token matches.
    Grammar_Immediate_SingleToken (const char *token)
	: token_match_cb (NULL)
    {
	this->token = grab (new String (token));
    }
};

#if 0
class TranzitionEntry : public SimplyReferenced
{
public:
// Deprecated comment    // If null, then '*' (any token) is assumed.
    Ref<String> token;
};
#endif

class TranzitionMatchEntry : public SimplyReferenced
{
public:
    Grammar_Immediate_SingleToken::TokenMatchCallback token_match_cb;

    TranzitionMatchEntry ()
	: token_match_cb (NULL)
    {
    }
};

class SwitchGrammarEntry : public SimplyReferenced
{
public:
    enum Flags {
	Dominating = 0x1 // Currently unused
    };

    Ref<Grammar> grammar;
    Uint32 flags;

    List< Ref<String> > variants;

//    List< Ref<TranzitionEntry> > tranzition_entries;
//    std::hash_set< Ref<TranzitionEntry> > tranzition_entries;

//    boost::unordered_set<std::string> tranzition_entries;

    class TranzitionEntry : public SimplyReferenced,
			    public M::HashEntry<>
    {
    public:
	Ref<String> grammar_name;
    };

    typedef M::Hash< TranzitionEntry,
		     MemoryDesc,
		     MemberExtractor< TranzitionEntry,
				      Ref<String>,
				      &TranzitionEntry::grammar_name,
				      MemoryDesc,
				      AccessorExtractor< String,
							 MemoryDesc,
							 &String::getMemoryDesc > >,
		     MemoryComparator<> >
	    TranzitionEntryHash;

    TranzitionEntryHash tranzition_entries;

    // TODO Use Map<>
    List< Ref<TranzitionMatchEntry> > tranzition_match_entries;
    bool any_tranzition;

    SwitchGrammarEntry ()
	: flags (0),
	  any_tranzition (false)
    {
    }

    ~SwitchGrammarEntry ();
};

class CompoundGrammarEntry : public SimplyReferenced
{
public:
    typedef void (*AssignmentFunc) (ParserElement *compound_element,
				    ParserElement *subel);

    class Acceptor : public Pargen::Acceptor
    {
    protected:
	AssignmentFunc assignment_func;
	ParserElement *compound_element;

    public:
	void setParserElement (ParserElement *parser_element)
	{
	    DEBUG (
		errf->print ("CompoundGrammarEntry.Acceptor.setParserElement: acceptor 0x").printHex ((Uint64) this).print (", parser_element 0x").printHex ((Uint64) parser_element).pendl ();
	    )

	    if (assignment_func != NULL)
		assignment_func (compound_element, parser_element);
	}

	void init (AssignmentFunc   const assignment_func,
		   ParserElement  * const compound_element /* non-null */)
	{
	    this->assignment_func = assignment_func;
	    this->compound_element = compound_element;
	}

	Acceptor (AssignmentFunc  assignment_func,
		  ParserElement  *compound_element)
	{
	    abortIf (compound_element == NULL);
	    this->assignment_func = assignment_func;
	    this->compound_element = compound_element;
	}

	Acceptor ()
	{
	}
    };

    enum Flags {
	Optional = 0x1,
	Sequence = 0x2
    };

  // TODO CompoundGrammarEntry_Jump,
  //      CompoundGrammarEntry_InlineMatch,
  //      CompoundGrammarEntry_Grammar

    // If 'is_jump' is non-null, then all non-jump fields should be considered
    // invalid.
    Bool is_jump;
    // Switch or compound
    Grammar *jump_grammar;
    Grammar::JumpFunc jump_cb;

    Size jump_switch_grammar_index;
    List< Ref<SwitchGrammarEntry> >::Element *jump_switch_grammar_entry;

    Size jump_compound_grammar_index;
    List< Ref<CompoundGrammarEntry> >::Element *jump_compound_grammar_entry;

    // If inline_match_func is non-null, then all other fields
    // should be considered invalid.
    Grammar::InlineMatchFunc inline_match_func;

    Ref<Grammar> grammar;
    Uint32 flags;

    AssignmentFunc assignment_func;

    static VSlab<Acceptor> acceptor_slab;

#if 0
    Ref<Acceptor> createAcceptorFor (ParserElement *compound_element)
    {
	return grab (new Acceptor (assignment_func, compound_element));
    }
#endif

    VSlabRef<Acceptor> createAcceptorFor (ParserElement *compound_element)
    {
	// TODO If assignment_func is NULL, then there's probably no need
	// in allocating the acceptor at all.
	VSlabRef<Acceptor> acceptor = VSlabRef<Acceptor>::forRef <Acceptor> (acceptor_slab.alloc ());
	acceptor->init (assignment_func, compound_element);
	return acceptor;
    }

    CompoundGrammarEntry ()
	: jump_grammar (NULL),
	  jump_cb (NULL),
	  jump_switch_grammar_index (0),
	  jump_switch_grammar_entry (NULL),
	  jump_compound_grammar_index (0),
	  jump_compound_grammar_entry (NULL),
	  inline_match_func (NULL),
	  flags (0),
	  assignment_func (NULL)
    {
    }
};

class Grammar_Compound : public Grammar
{
public:
//    typedef Ref<ParserElement> (*ElementCreationFunc) ();
    typedef ParserElement* (*ElementCreationFunc) (VStack * const vstack /* non-null */);

    Ref<String> name;

    ElementCreationFunc elem_creation_func;
    List< Ref<CompoundGrammarEntry> > grammar_entries;

    Ref<String> toString ();

    List< Ref<CompoundGrammarEntry> >::Element* getSecondSubgrammarElement ()
    {
	Size i = 0;
	List< Ref<CompoundGrammarEntry> >::Iterator ge_iter (grammar_entries);
	while (!ge_iter.done ()) {
	    List< Ref<CompoundGrammarEntry> >::Element &ge_el = ge_iter.next ();
	    if (ge_el.data->inline_match_func == NULL) {
		i ++;
		if (i > 1)
		    return &ge_el;
	    }
	}

	return NULL;
    }

    CompoundGrammarEntry* getFirstSubgrammarEntry ()
    {
	List< Ref<CompoundGrammarEntry> >::DataIterator ge_iter (grammar_entries);
	while (!ge_iter.done ()) {
	    Ref<CompoundGrammarEntry> &ge = ge_iter.next ();
	    if (ge->inline_match_func == NULL)
		return ge;
	}

	return NULL;
    }

    Grammar* getFirstSubgrammar ()
    {
	CompoundGrammarEntry * const ge = getFirstSubgrammarEntry ();
	if (ge == NULL)
	    return NULL;

	return ge->grammar;
    }

    ParserElement* createParserElement (VStack * const vstack /* non-null */)
    {
	return elem_creation_func (vstack);
    }

    Grammar_Compound (ElementCreationFunc elem_creation_func)
	: Grammar (Grammar::t_Compound)
    {
	this->elem_creation_func = elem_creation_func;
    }
};

class Grammar_Switch : public Grammar
{
public:
    Ref<String> name;

    List< Ref<SwitchGrammarEntry> > grammar_entries;

    Ref<String> toString ();

    Grammar_Switch ()
	: Grammar (Grammar::t_Switch)
    {
    }
};

class Grammar_Alias : public Grammar
{
public:
    Ref<String> name;

    Ref<Grammar> aliased_grammar;

    Ref<String> toString ()
    {
	// TODO
	return String::forData ("--- Alias ---");
    }

    Grammar_Alias ()
	: Grammar (Grammar::t_Alias)
    {
    }
};

}


#undef DEBUG


#include <pargen/parser.h>

#endif /* __PARGEN__GRAMMAR_H__ */


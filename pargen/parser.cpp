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


// NO BOOST
//#include <boost/unordered/unordered_set.hpp>
//#include <boost/intrusive/list.hpp>
//#include <boost/intrusive/avl_set.hpp>

#include <mycpp/io.h>
#include <mycpp/util.h>

#include <pargen/parser.h>


#define VSLAB_ACCEPTOR


/* [DMS] This is probably among the worst pieces of code I have ever written.
 * But since this is all about managed non-stack recursion, it is also
 * one of the most unusual pieces of code. Take it as it is :)
 */

// Parsing trace, should be always enabled.
// Enabled/disabled by parsing_state->debug_dump flag.
#define DEBUG_PAR(a) a

#define DEBUG(a) ;
// Flow
#define DEBUG_FLO(a) ;
// Internal
#define DEBUG_INT(a) ;
// Callbacks
#define DEBUG_CB(a) ;
// Optimization
#define DEBUG_OPT(a) ;
#define DEBUG_OPT2(a) ;
// Negative cache
#define DEBUG_NEGC(a) ;
#define DEBUG_NEGC2(a) ;
// VStack memory allocation
#define DEBUG_VSTACK(a) ;

#define FUNC_NAME(a) ;


// Enables forward optimization (single-token lookahead)
#define PARGEN_FORWARD_OPTIMIZATION

// Enables negative cache lookups
#define PARGEN_NEGATIVE_CACHE

// Enables upwards jumps
#define PARGEN_UPWARDS_JUMPS


using namespace MyCpp;
using namespace MyLang;

namespace Pargen {

Ref<ParserConfig>
createParserConfig (bool const upwards_jumps)
{
    Ref<ParserConfig> parser_config = grab (new ParserConfig);
    parser_config->upwards_jumps = upwards_jumps;
    return parser_config;
}

Ref<ParserConfig>
createDefaultParserConfig ()
{
    return createParserConfig (true /* upwards_jumps */);
}

namespace {
    class ParsingState;
}

static void pop_step (ParsingState *parsing_state,
		      bool match,
		      bool empty_match,
		      bool negative_cache_update = true);

namespace {

    class ParsingStep : public virtual SimplyReferenced,
			public IntrusiveListElement<>
    {
    public:
	typedef void (*AssignmentFunc) (ParserElement *parser_element,
					void          *user_data);

	enum Type {
	    t_Sequence,
	    t_Compound,
	    t_Switch,
	    t_Alias
	};

	const Type parsing_step_type;

	Grammar *grammar;

#ifndef VSLAB_ACCEPTOR
	Ref<Acceptor> acceptor;
#else
	// TODO Why not use steps vstack to hold the acceptor?
	VSlabRef<Acceptor> acceptor;
#endif
	Bool optional;

	// Initialized in push_step()
	TokenStream::PositionMarker token_stream_pos;

	Size go_right_count;

	VStack::Level vstack_level;
	VStack::Level el_level;

	ParsingStep (Type type)
	    : parsing_step_type (type),
	      grammar (NULL),
	      go_right_count (0)
	{
	}

	~ParsingStep ()
	{
	  DEBUG_INT (
	    errf->print ("Pargen.ParsingStep.~()").pendl ();
	  )
	}
    };

    typedef IntrusiveList<ParsingStep> ParsingStepList;

    class ParsingStep_Sequence : public ParsingStep
    {
    public:
	// FIXME Memory leak + inefficient: use intrusive list.
	List<ParserElement*> parser_elements;

	ParsingStep_Sequence ()
	    : ParsingStep (ParsingStep::t_Sequence)
	{
	}
    };

    class ParsingStep_Compound : public ParsingStep
    {
    public:
	List< Ref<CompoundGrammarEntry> >::Element *cur_subg_el;

	Bool got_jump;
	Grammar *jump_grammar;
	Grammar::JumpFunc jump_cb;
	List< Ref<SwitchGrammarEntry> >::Element *jump_switch_grammar_entry;
	List< Ref<CompoundGrammarEntry> >::Element *jump_compound_grammar_entry;

	Bool jump_performed;

	// We should be able to detect left-recursive grammars for "a: b_opt a c"
	// cases when b_opt doesn't match, hence this hint.
	Grammar *lr_parent;

	ParserElement *parser_element;

	Bool got_nonoptional_match;

	ParsingStep_Compound ()
	    : ParsingStep (ParsingStep::t_Compound),
	      jump_grammar (NULL),
	      jump_cb (NULL),
	      jump_switch_grammar_entry (NULL),
	      jump_compound_grammar_entry (NULL),
	      lr_parent (NULL),
	      parser_element (NULL)
	{
	}
    };

    class ParsingStep_Switch : public ParsingStep
    {
    public:
	enum State {
	    // Parsing non-left-recursive grammars
	    State_NLR,
	    // Parsing left-recursive grammars
	    State_LR
	};

	State state;

	Bool got_empty_nlr_match;
	Bool got_nonempty_nlr_match;
	Bool got_lr_match;

	List< Ref<SwitchGrammarEntry> >::Element *cur_nlr_el;
	List< Ref<SwitchGrammarEntry> >::Element *cur_lr_el;

	ParserElement *nlr_parser_element;
	ParserElement *parser_element;

	ParsingStep_Switch ()
	    : ParsingStep (ParsingStep::t_Switch),
	      nlr_parser_element (NULL),
	      parser_element (NULL)
	{
	}
    };

    class ParsingStep_Alias : public ParsingStep
    {
    public:
	ParserElement *parser_element;

	ParsingStep_Alias ()
	    : ParsingStep (ParsingStep::t_Alias),
	      parser_element (NULL)
	{
	}
    };

    // Positive cache is an n-ary tree with chains of matching phrases.
    // Nodes of the tree refer to grammars.
    //
    // Consider the following grammar:
    //
    //     a: b c
    //     b: [b]
    //     c: d e
    //     d: [d]
    //     e: [e]
    //     f: [f]
    //
    // And input: "b d e f".
    //
    // Here's the state of the cache after parsing this input:
    //
    //        +--->a------------------------->f
    //        |                              /^
    //        |      +----->c---------------/ |
    //        |      |                        |
    // (root)-+--->b-+----->d------->e--------+
    //
    // Tokens:     o--------o--------o--------o
    //             b        d        e        f
    //
    // Positive cache is cleaned at cache cleanup points. Such points are
    // specified explicitly in the grammar. They are usually points of
    // no return, after wich match failures mean syntax errors in input.
    //
    class PositiveCacheEntry : public SimplyReferenced
    {
    public:
	Bool match;
	Grammar *grammar;

	PositiveCacheEntry *parent_entry;

	typedef Map< Ref<PositiveCacheEntry>,
		     MemberExtractor< PositiveCacheEntry const,
				      Grammar* const,
				      &PositiveCacheEntry::grammar,
				      UidType,
				      AccessorExtractor< UidProvider const,
							 UidType,
							 &Grammar::getUid > >,
		     DirectComparator<UidType> >
		PositiveMap;

	PositiveMap positive_map;

	PositiveCacheEntry ()
	    : grammar (NULL),
	      parent_entry (NULL)
	{
	}
    };

    // TODO Having a similar superclass for positive cache would be nice.
    class NegativeCache
    {
    private:
	class GrammarEntry : public IntrusiveAvlTree_Node<>
	{
	public:
	    Grammar *grammar;
//	    VSlab<GrammarEntry>::AllocKey slab_key;

#if 0
	    bool operator < (GrammarEntry const &grammar_entry) const
	    {
		return (Size) grammar < (Size) grammar_entry.grammar;
	    }

	    bool operator < (Grammar * const &ext_grammar) const
	    {
		return (Size) grammar < (Size) ext_grammar;
	    }
#endif
	};

	class NegEntry : //public SimplyReferenced
			 public IntrusiveListElement<>
	{
	public:
	    // TODO Unused?
	    typedef Map< Grammar*,
			 AccessorExtractorEx< Grammar,
					      UidProvider const,
					      UidType,
					      &Grammar::getUid >,
			 DirectComparator<UidType> >
		    NegativeMap;

	    // TODO intrusive map
//	    NegativeMap negative_map;
//	    boost::unordered_set<Grammar*> neg_set;

	    typedef IntrusiveAvlTree< GrammarEntry,
				      MemberExtractor< GrammarEntry,
						       Grammar*,
						       &GrammarEntry::grammar,
						       UintPtr,
						       CastExtractor< Grammar*,
								      UintPtr > >,
				      DirectComparator<UintPtr> >
		    GrammarEntryTree;

	    GrammarEntryTree grammar_entries;
	};

	// Note: There's no crucial reason to do this.
	typedef IntrusiveList<NegEntry> NegEntryList;

	VStack neg_vstack;
	VSlab<GrammarEntry> grammar_slab;

	NegEntryList neg_cache;
	NegEntry *cur_neg_entry;
//	NegEntryList::iterator cur_iter;

//	List< Ref<NegEntry> > neg_cache;
//	List< Ref<NegEntry> >::Element *cur_neg_el;

	// For debugging
	size_t pos_index;

    public:
	void goRight ()
	{
	  FUNC_NAME (
	    static char const * const _func_name = "Pargen.NegativeCache.goRight";
	  )

	    if (cur_neg_entry == NULL ||
		cur_neg_entry == neg_cache.getLast())
	    {
		NegEntry * const neg_entry =
			new (neg_vstack.push_malign (sizeof (NegEntry))) NegEntry;
		neg_cache.append (neg_entry);
		cur_neg_entry = neg_cache.getLast();
	    } else {
//		++ cur_iter;
		cur_neg_entry = neg_cache.getNext (cur_neg_entry);
	    }

	    ++ pos_index;
	    DEBUG_NEGC2 (
		errf->print (_func_name).print (": pos_index ").print (pos_index).pendl ();
	    )
	}

	void goLeft ()
	{
	  FUNC_NAME (
	    static char const * const _func_name = "Pargen.NegativeCache.goLeft";
	  )

	    if (cur_neg_entry == NULL ||
		cur_neg_entry == neg_cache.getFirst())
	    {
		NegEntry * const neg_entry =
			new (neg_vstack.push_malign (sizeof (NegEntry))) NegEntry;
//		cur_iter = neg_cache.insert (cur_iter, *neg_entry);
		neg_cache.append (neg_entry, cur_neg_entry /* to_el */);
		cur_neg_entry = neg_entry;
	    } else {
//		-- cur_iter;
		cur_neg_entry = neg_cache.getPrevious (cur_neg_entry);
	    }

	    -- pos_index;
	    DEBUG_NEGC2 (
		errf->print (_func_name).print (": pos_index ").print (pos_index).pendl ();
	    )
	}

	void addNegative (Grammar * const grammar)
	{
	  FUNC_NAME (
	    static char const * const _func_name = "Pargen.NegativeCache.addNegative";
	  )

	    abortIf (cur_neg_entry == NULL);

	    DEBUG_NEGC2 (
		errf->print (_func_name).print (": pos_index ").print (pos_index).pendl ();
	    )

#if 0
// Deprecated
	    if (!(*cur_iter).negative_map.lookupValue (grammar).isNull ())
		return;

	    (*cur_iter).negative_map.add (grammar);
#endif

//	    (*cur_iter).neg_set.insert (grammar);

	    VSlab<GrammarEntry>::AllocKey slab_key;
	    GrammarEntry * const grammar_entry = grammar_slab.alloc (&slab_key);
	    grammar_entry->grammar = grammar;
	    if (!cur_neg_entry->grammar_entries.addUnique (grammar_entry)) {
	      // New element inserted.

//		grammar_entry->slab_key = slab_key;
	    } else {
	      // Element with the same value already exists.

		DEBUG_NEGC (
		    errf->print (_func_name).print (": duplicate entry").pendl ();
		)

		grammar_slab.free (slab_key);
	    }
	}

#if 0
	class TmpComp
	{
	public:
	    bool operator () (GrammarEntry const &grammar_entry, Grammar * const grammar) const
	    {
		return (Size) grammar_entry.grammar < (Size) grammar;
	    }

	    bool operator () (Grammar * const grammar, GrammarEntry const &grammar_entry) const
	    {
		return (Size) grammar < (Size) grammar_entry.grammar;
	    }
	};
#endif

	bool isNegative (Grammar * const grammar)
	{
	  FUNC_NAME (
	    static char const * const _func_name = "Pargen.NegativeCache.isNegative";
	  )

	    abortIf (cur_neg_entry == NULL);

	    DEBUG_NEGC2 (
		errf->print (_func_name).print (": pos_index ").print (pos_index).pendl ();
	    )

//	    return !(*cur_iter).negative_map.lookupValue (grammar).isNull ();
//	    return (*cur_iter).neg_set.find (grammar) != (*cur_iter).neg_set.end ();

	    return cur_neg_entry->grammar_entries.lookup ((UintPtr) grammar /* , TmpComp () */);
	}

	void cut ()
	{
//	    NegEntryList::iterator iter = neg_cache.begin ();
	    NegEntry *neg_entry = neg_cache.getFirst();
//	    while (iter != neg_cache.end ()) {
	    while (neg_entry) {
		NegEntry * const next_neg_entry = neg_cache.getNext (neg_entry);

//		if (iter == cur_iter)
		if (neg_entry == cur_neg_entry)
		    break;

//		iter = neg_cache.erase (iter);
		neg_cache.remove (neg_entry);

		// TODO FIXME Forgot to cut neg_vstack?

		// TODO vstack->vqueue, cut queue tail

		neg_entry = next_neg_entry;
	    }
	}

	NegativeCache ()
	    : neg_vstack (1 << 16),
	      cur_neg_entry (NULL),
	      pos_index (0)
	{
	}
    };

    // State of the parser.
    class ParsingState : public ParserControl,
			 public virtual SimplyReferenced
    {
    public:
	enum Direction {
	    Up,
	    Down
	};

	class PositionMarker : public ParserPositionMarker,
			       public virtual SimplyReferenced
	{
	public:
	    TokenStream::PositionMarker token_stream_pos;
// VSTACK	    Ref<ParsingStep_Compound> compound_step;
	    ParsingStep_Compound *compound_step;
	    List< Ref<CompoundGrammarEntry> >::Element *cur_subg_el;
	    Bool got_nonoptional_match;
	    Size go_right_count;

	    PositionMarker ()
		: compound_step (NULL)
	    {
	    }
	};

	Ref<ParserConfig> parser_config;

	ConstMemoryDesc default_variant;

	VSlab< ListAcceptor<ParserElement> > list_acceptor_slab;
	VSlab< PtrAcceptor<ParserElement> > ptr_acceptor_slab;

	Bool create_elements;

	Ref<String> variant;

	// Nest level is used for debugging output.
	Size nest_level;

	Ref<TokenStream> token_stream;
	Ref<LookupData> lookup_data;
	// User data for accept_func() and match_func().
	void *user_data;

	// FIXME Temporarily persistent
	VStack *el_vstack;

	// Stack of grammar invocations.
// Deprecated	List< Ref<ParsingStep> > steps;
	ParsingStepList step_list;
	VStack step_vstack;

	// Direction of the previous movement along the stack:
	// "Up" means we've become one level deeper ('steps' grew),
	// "Down" means we've returned from a nested level ('steps' shrunk).
	// Note: it looks like I've swapped the meanings for "up" and "down" here.
	Direction cur_direction;

	// 'true' if we've come from a nested grammar with a match for that grammar,
	// 'false' otherwise.
	Bool match;
	// 'true' if we've come from a nested grammar with an empty match
	// for that grammar.
	Bool empty_match;

	// 'true' if we're up from a grammar which turns out
	// to be left recursive after attempting to parse it.
	// ("a: b_opt a c", and "b" is no match).
	Bool compound_lr;

	// 'true' if we're up from a compound grammar which is an empty match.
	// This is possible if all subgrammars are optional or if we preset
	// the first element of a left-recursive grammar and all of the following
	// elements are optional.
	Bool compound_empty;

	Bool debug_dump;

	PositiveCacheEntry  positive_cache_root;
	PositiveCacheEntry *cur_positive_cache_entry;

	NegativeCache negative_cache;

	Bool position_changed;

	ParsingStep& getLastStep ()
	{
	    abortIf (step_list.isEmpty());
	    return *step_list.getLast();
	}

	void setCreateElements (bool create_elements)
	{
	    this->create_elements = create_elements;
	}

	Ref<ParserPositionMarker> getPosition ();

	void setPosition (ParserPositionMarker *pmark);

	void setVariant (ConstMemoryDesc const &variant)
	{
	    this->variant = grab (new String (variant));
	}

	ParsingState ()
	    : el_vstack (new VStack (1 << 16 /* block_size */)),
	      step_vstack (1 << 16 /* block_size */)
	{
	  FUNC_NAME (
	    static char const * const _func_name = "Pargen.ParsingState.()";
	  )

	    DEBUG_VSTACK (
		errf->print (_func_name).print (": "
			     "list_acceptor_slab vstack: 0x").printHex ((Size) &list_acceptor_slab.vstack).print (", "
			     "ptr_acceptor_slab vstack: 0x").printHex ((Size) &ptr_acceptor_slab.vstack).pendl ();
		errf->print (_func_name).print (": "
			     "el_vstack: 0x").printHex ((Size) el_vstack).print (", "
			     "step_vstack: 0x").printHex ((Size) &step_vstack).pendl ();
		errf->print (_func_name).print (": CompoundGrammarEntry::acceptor_slab vstack: "
			     "0x").printHex ((Size) &CompoundGrammarEntry::acceptor_slab.vstack).pendl ();
	    )
	}
    };

    Ref<ParserPositionMarker>
    ParsingState::getPosition ()
    {
	ParsingStep &parsing_step = getLastStep ();
	abortIf (parsing_step.parsing_step_type != ParsingStep::t_Compound);
	ParsingStep_Compound *compound_step = static_cast <ParsingStep_Compound*> (&parsing_step);

	Ref<PositionMarker> pmark = grab (new PositionMarker);
	token_stream->getPosition (&pmark->token_stream_pos);
	pmark->compound_step = compound_step;
	pmark->got_nonoptional_match = compound_step->got_nonoptional_match;
	pmark->go_right_count = parsing_step.go_right_count;

	{
#if 0
// TODO Зачем это было написано (нееверный блок)?
	    List< Ref<CompoundGrammarEntry> >::Element *next_subg_el = compound_step->cur_subg_el;
	    while (next_subg_el != NULL &&
		   // FIXME Спорно. Скорее всего, нужно пропускать
		   //       только первую match-функцию - ту, которая вызвана
		   //       в данный момент.
		   next_subg_el->data->inline_match_func != NULL)
	    {
		next_subg_el = next_subg_el->next;
	    }

	    pmark->cur_subg_el = next_subg_el;
#endif

	    pmark->cur_subg_el = compound_step->cur_subg_el;
	}

	return pmark.ptr ();
    }

    void
    ParsingState::setPosition (ParserPositionMarker *_pmark)
    {
	FUNC_NAME (
	    static char const * const _func_name = "Pargen.ParsingState.setPosition";
	)

	PositionMarker *pmark = static_cast <PositionMarker*> (_pmark);

	position_changed = true;

	Size total_go_right = 0;
	{
// VSTACK	    ParsingStep * const mark_step = static_cast <ParsingStep*> (pmark->compound_step.ptr ());
	    ParsingStep * const mark_step = static_cast <ParsingStep*> (pmark->compound_step);
	    for (;;) {
		ParsingStep * const cur_step = step_list.getLast();
		if (cur_step == mark_step)
		    break;

#if 0
		for (Size i = 0; i < cur_step->go_right_count; i++) {
		    DEBUG_NEGC (
			errf->print (_func_name).print (": go left").pendl ();
		    )
		    negative_cache.goLeft ();
		}
#endif
		total_go_right += cur_step->go_right_count;

		pop_step (this, false /* match */, false /* empty_match */, false /* negative_cache_update */);
	    }
	}

	ParsingStep_Compound *compound_step = static_cast <ParsingStep_Compound*> (&getLastStep ());

	compound_step->cur_subg_el = pmark->cur_subg_el;
	compound_step->got_nonoptional_match = pmark->got_nonoptional_match;

	cur_direction = ParsingState::Up;

	total_go_right += compound_step->go_right_count;
	abortIf (total_go_right < pmark->go_right_count);
//	abortIf (compound_step->go_right_count < pmark->go_right_count);
//	for (Size i = 0; i < compound_step->go_right_count - pmark->go_right_count; i++) {
	for (Size i = 0; i < total_go_right - pmark->go_right_count; i++) {
	    DEBUG_NEGC (
		errf->print (_func_name).print (": go left").pendl ();
	    )
	    negative_cache.goLeft ();
	}

	token_stream->setPosition (&pmark->token_stream_pos);
    }

}

static void
print_whsp (File *file,
	    Size num_spaces)
{
    for (Size i = 0; i < num_spaces; i++)
	file->print (" ");
}

static void
print_tab (File *file,
	   Size nest_level)
{
    print_whsp (file, nest_level * 1);
}

static void
push_step (ParsingState * const parsing_state,
	   ParsingStep  * const step,
	   Bool           const new_checkpoint = true)
{
    DEBUG_INT (
      errf->print ("Pargen.push_step").pendl ();
    );

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

    parsing_state->token_stream->getPosition (&step->token_stream_pos);

    parsing_state->nest_level ++;

    {
	// TEST FIXME TEMPORAL
	step->ref ();

	parsing_state->step_list.append (step);
	step->ref ();
    }

    parsing_state->cur_direction = ParsingState::Up;

    if (new_checkpoint) {
	if (!parsing_state->lookup_data.isNull ())
	    parsing_state->lookup_data->newCheckpoint ();
    }

    DEBUG_PAR (
	if (parsing_state->debug_dump) {
	    errf->print (">");
	    print_tab (errf, parsing_state->nest_level);

	    ParsingStep &_step = parsing_state->getLastStep ();
	    switch (_step.parsing_step_type) {
		case ParsingStep::t_Sequence: {
		    ParsingStep_Sequence &step = static_cast <ParsingStep_Sequence&> (_step);
		    errf->print ("(seq) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Compound: {
		    ParsingStep_Compound &step = static_cast <ParsingStep_Compound&> (_step);
		    errf->print ("(com) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Switch: {
		    ParsingStep_Switch &step = static_cast <ParsingStep_Switch&> (_step);
		    errf->print ("(swi) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Alias: {
		    errf->print ("(alias)");
		}
	    }

	    errf->print (" >").pendl ();
	}
    )
}

static void
pop_step (ParsingState *parsing_state,
	  bool match,
	  bool empty_match,
	  bool negative_cache_update)
{
  FUNC_NAME (
    static char const * const _func_name = "Pargen.Parser.pop_step";
  )

    abortIf (empty_match && !match);

    DEBUG_INT (
	errf->print (_func_name).print (": match: ").print (match ? "true" : "false").print (
		     ", empty_match: ").print (empty_match ? "true" : "false").pendl ();
    );

    DEBUG_PAR (
	if (parsing_state->debug_dump) {
	    if (match)
		errf->print ("+");
	    else
		errf->print (" ");

	    print_tab (errf, parsing_state->nest_level);

	    if (match)
		errf->print ("MATCH ");

	    ParsingStep &_step = parsing_state->getLastStep ();
	    switch (_step.parsing_step_type) {
		case ParsingStep::t_Sequence: {
		    ParsingStep_Sequence &step = static_cast <ParsingStep_Sequence&> (_step);
		    errf->print ("(seq) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Compound: {
		    ParsingStep_Compound &step = static_cast <ParsingStep_Compound&> (_step);
		    errf->print ("(com) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Switch: {
		    ParsingStep_Switch &step = static_cast <ParsingStep_Switch&> (_step);
		    errf->print ("(swi) ").print (step.grammar->toString ());
		} break;
		case ParsingStep::t_Alias: {
		    errf->print ("(alias)");
		} break;
	    }

	    if (match)
		errf->print (" +");
	    else
		errf->print (" <");

	    errf->pendl ();
	}
    );

    abortIf (parsing_state == NULL);

    abortIf (parsing_state->step_list.isEmpty());
    ParsingStep &step = *parsing_state->step_list.getLast();

    if (!match || empty_match)
	parsing_state->token_stream->setPosition (&step.token_stream_pos);

    if (negative_cache_update) {
	if (!match || empty_match) {
	    for (Size i = 0; i < step.go_right_count; i++) {
		DEBUG_NEGC (
		    errf->print (_func_name).print (": go left").pendl ();
		)
		parsing_state->negative_cache.goLeft ();
	    }
	} else {
	    if (parsing_state->step_list.getFirst() != parsing_state->step_list.getLast()) {
//		ParsingStep &prv_step = *(-- -- parsing_state->step_list.end ());
		ParsingStep &prv_step = *(parsing_state->step_list.getPrevious (parsing_state->step_list.getLast()));
		prv_step.go_right_count += step.go_right_count;
	    }
	}

	if (!match /* TEST && !empty_match */) {
	    DEBUG_NEGC (
		errf->print (_func_name).print (": adding negative ").print (step.grammar->toString ()).pendl ();
	    )
	    parsing_state->negative_cache.addNegative (step.grammar);
	}
    }

    parsing_state->match = match;
    parsing_state->empty_match = empty_match;

    parsing_state->nest_level --;
    {
// Deprecated	parsing_state->steps.remove (parsing_state->steps.last);

	ParsingStep * const tmp_step = parsing_state->step_list.getLast();
	VStack::Level const tmp_level = tmp_step->vstack_level;
	VStack::Level const tmp_el_level = tmp_step->el_level;

	parsing_state->step_list.remove (parsing_state->step_list.getLast());
	tmp_step->~ParsingStep ();

// VSTACK	tmp_step->unref ();
	parsing_state->step_vstack.setLevel (tmp_level);

	if (!match) {
//	    errf->print ("--- setting el_level").pendl ();
	    parsing_state->el_vstack->setLevel (tmp_el_level);
	}
    }

    parsing_state->cur_direction = ParsingState::Down;

    if (match) {
	if (!parsing_state->lookup_data.isNull ())
	    parsing_state->lookup_data->commitCheckpoint ();
    } else {
	if (!parsing_state->lookup_data.isNull ())
	    parsing_state->lookup_data->cancelCheckpoint ();
    }
}

// Returns 'true' if we have a match, 'false otherwise.
static bool
parse_Immediate (ParsingState      * const parsing_state,
		 Grammar_Immediate * const grammar,
		 Acceptor          * const acceptor)
{
    static char const * const _func_name = "Pargen.Parser.parse_Immediate";

    DEBUG_FLO (
      errf->print (_func_name).pendl ();
    );

    abortIf (parsing_state == NULL);

    TokenStream::PositionMarker pmark;
    parsing_state->token_stream->getPosition (&pmark);

    Ref<SimplyReferenced> user_obj;
    void *user_ptr;
    ConstMemoryDesc token = parsing_state->token_stream->getNextToken (&user_obj, &user_ptr);
    if (token.getLength () == 0) {
	parsing_state->token_stream->setPosition (&pmark);
	DEBUG (
	    errf->print (_func_name).print (": no token").pendl ();
	)
	return false;
    }

    DEBUG_PAR (
	if (parsing_state->debug_dump)
	    errf->print (_func_name).print (": token: ").print (token).pendl ();
    )

    DEBUG_INT (
	errf->print (_func_name).print (": token: ").print (token).pendl ();
    );

    if (!grammar->match (token, user_ptr, parsing_state->user_data)) {
	parsing_state->token_stream->setPosition (&pmark);

	DEBUG_INT (
	    errf->print (_func_name).print (": !grammar->match()").pendl ();
	)

	return false;
    }

    // Note: This is a strange condition...
    if (acceptor != NULL) {
	Byte * const el_token_buf = parsing_state->el_vstack->push (token.getLength ());
	memcpy (el_token_buf, token.getMemory (), token.getLength ());
	ParserElement * const parser_element =
		new (parsing_state->el_vstack->push_malign (sizeof (ParserElement_Token))) ParserElement_Token (
			ConstMemoryDesc (el_token_buf, token.getLength ()),
			user_ptr);
//		grab (static_cast <ParserElement*> (new ParserElement_Token (ConstMemoryDesc (el_token_buf, token.getLength ()),
//									     user_ptr)));

	// TODO I think that match_func() and accept_func() should be called
	// regardless of whether 'acceptor' is NULL or not.
	if (grammar->match_func != NULL) {
	    DEBUG_CB (
		errf->print (_func_name).print (": calling match_func()").pendl ();
	    )
	    if (!grammar->match_func (parser_element, parsing_state, parsing_state->user_data)) {
		parsing_state->token_stream->setPosition (&pmark);
		DEBUG_INT (
		    errf->print (_func_name).print (": match_func() returned false").pendl ();
		)
		return false;
	    }

	    DEBUG_INT (
		errf->print (_func_name).print (": match_func() returned true").pendl ();
	    )
	} else {
	    DEBUG_INT (
		errf->print (_func_name).print (": no match_func()").pendl ();
	    )
	}

	if (grammar->accept_func != NULL) {
	    DEBUG_CB (
		errf->print (_func_name).print (": calling accept_func()").pendl ();
	    )
	    grammar->accept_func (parser_element,
				  parsing_state,
				  parsing_state->user_data);
	}

	acceptor->setParserElement (parser_element);

//	errf->print (_func_name).print (": new token parser_element: 0x").printHex ((Uint64) parser_element).pendl ();
    }

    return true;
}

enum ParsingResult {
    ParseUp,
    ParseNonemptyMatch,
    ParseEmptyMatch,
    ParseNoMatch
};

static void
push_compound_step (ParsingState     * const parsing_state,
		    Grammar_Compound * const grammar,
#ifndef VSLAB_ACCEPTOR
		    Acceptor         * const acceptor,
#else
		    VSlabRef<Acceptor> const acceptor,
#endif
		    bool               const optional,
		    bool               const got_nonoptional_match = false,
		    Size               const go_right_count = 0,
		    bool               const got_cur_subg_el = false,
		    List< Ref<CompoundGrammarEntry> >::Element * const cur_subg_el = NULL)
{
//    static char const * const _func_name = "Pargen.Parser.push_compound_step";

// VSTACK    Ref<ParsingStep_Compound> step = grab (new ParsingStep_Compound);
    VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
    VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
    ParsingStep_Compound * const step =
	    new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Compound))) ParsingStep_Compound;
    step->vstack_level = tmp_vstack_level;
    step->el_level = tmp_el_level;
    step->acceptor = acceptor;
    step->optional = optional;
    step->grammar = grammar;
    step->got_nonoptional_match = got_nonoptional_match;
    step->go_right_count = go_right_count;

    if (got_cur_subg_el)
	step->cur_subg_el = cur_subg_el;
    else
	step->cur_subg_el = grammar->grammar_entries.first;

    step->parser_element = grammar->createParserElement (parsing_state->el_vstack);
    push_step (parsing_state, step);

//    errf->print (_func_name).print (": new parser_element: 0x").printHex ((Uint64) step->parser_element).pendl ();
}

static void
push_switch_step (ParsingState       * const parsing_state,
		  Grammar_Switch     * const grammar,
#ifndef VSLAB_ACCEPTOR
		  Acceptor           * const acceptor,
#else
		  VSlabRef<Acceptor>   const acceptor,
#endif
		  bool                 const optional,
		  List< Ref<SwitchGrammarEntry> >::Element * const cur_subg_el = NULL)
{
// VSTACK    Ref<ParsingStep_Switch> step = grab (new ParsingStep_Switch);
    VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
    VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
    ParsingStep_Switch * const step =
	    new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Switch))) ParsingStep_Switch;
    step->vstack_level = tmp_vstack_level;
    step->el_level = tmp_el_level;
    step->acceptor = acceptor;
    step->optional = optional;
    step->grammar = grammar;
    step->state = ParsingStep_Switch::State_NLR;
    step->got_empty_nlr_match = false;
    step->got_nonempty_nlr_match = false;
    step->got_lr_match = false;

    if (cur_subg_el != NULL)
	step->cur_nlr_el = cur_subg_el;
    else
	step->cur_nlr_el = grammar->grammar_entries.first;

    step->cur_lr_el = NULL;

    push_step (parsing_state, step);
}

static ParsingResult
parse_grammar (ParsingState *parsing_state,
	       Grammar      *_grammar,
// VSLAB ACCEPTOR	       Acceptor     *acceptor,
	       VSlabRef<Acceptor> acceptor,
	       bool          optional)
{
  FUNC_NAME (
    static char const * const _func_name = "Pargen.Parser.parse_grammar";
  )

    DEBUG_FLO (
      errf->print (_func_name).pendl ();
    );

    abortIf (parsing_state == NULL);
    abortIf (_grammar == NULL);

#ifdef PARGEN_NEGATIVE_CACHE
    if (parsing_state->negative_cache.isNegative (_grammar)) {
      // TODO At this point, we have already checked that the grammar is
      // in negative cache for the current token. But we'll test for this
      // again in pop_step(), which could be avoided if we remembered the result
      // of the lookup that we've just made.

	DEBUG_NEGC (
	    errf->print (_func_name).print (": negative ").print (_grammar->toString ()).pendl ();
	)

//	if (parsing_state->debug_dump)
//	    errf->print ("!");

	if (optional) {
#if 0
	  // FIXME: This looks strange. Why don't we expect this to be called
	  // in parse_Immediate()? This calls seems to be excessive.
	    if (_grammar->accept_func != NULL) {
		_grammar->accept_func (NULL,
				       parsing_state,
				       parsing_state->user_data);
	    }
#endif

	    return ParseEmptyMatch;
	}

	return ParseNoMatch;
    }
#endif

    switch (_grammar->grammar_type) {
	case Grammar::t_Immediate: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": Grammar::_Immediate").pendl ();
	    );
	    Bool match = parse_Immediate (parsing_state,
					  static_cast <Grammar_Immediate*> (_grammar),
					  acceptor);

	    if (match) {
		{
		  // Updating negative cache state (moving right)

		    DEBUG_NEGC (
			errf->print (_func_name).print (": go right").pendl ();
		    )
		    parsing_state->negative_cache.goRight ();

		    if (!parsing_state->step_list.isEmpty()) {
			ParsingStep * const step = parsing_state->step_list.getLast();
			step->go_right_count ++;
		    }
		}

		return ParseNonemptyMatch;
	    }

	    if (optional) {
	      // FIXME: This looks strange. Why don't we expect this to be called
	      // in parse_Immediate()? This calls seems to be excessive.

		abortIf (match);
		if (_grammar->accept_func != NULL) {
		    _grammar->accept_func (NULL,
					   parsing_state,
					   parsing_state->user_data);
		}

		return ParseEmptyMatch;
	    }

	    return ParseNoMatch;
	} break;

	case Grammar::t_Compound: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": Grammar::_Compound").pendl ();
	    );

	    Grammar_Compound * const grammar = static_cast <Grammar_Compound*> (_grammar);

	    push_compound_step (parsing_state,
				grammar,
				acceptor,
				optional,
				false /* got_nonoptional_match */,
				0     /* go_right_count */,
				false /* got_cur_subg_el */,
				NULL  /* cur_subg_el */);
	} break;

	case Grammar::t_Switch: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": Grammar::_Switch").pendl ();
	    );

	    Grammar_Switch * const &grammar = static_cast <Grammar_Switch*> (_grammar);

	    push_switch_step (parsing_state, grammar, acceptor, optional, NULL /* cur_subg_el */);
	} break;

	case Grammar::t_Alias: {
	    DEBUG_INT (
		errf->print (_func_name).print (": Grammar::_Alias").pendl ();
	    )
	    Grammar_Alias * const &grammar = static_cast <Grammar_Alias*> (_grammar);
// VSTACK	    Ref<ParsingStep_Alias> step = grab (new ParsingStep_Alias);
	    VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
	    VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
	    ParsingStep_Alias * const step =
		    new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Alias))) ParsingStep_Alias;
	    step->vstack_level = tmp_vstack_level;
	    step->el_level = tmp_el_level;
	    step->acceptor = acceptor;
	    step->optional = optional;
	    step->grammar = grammar;
	    push_step (parsing_state, step);
	} break;

	default:
	    abortIfReached ();
    }

    return ParseUp;
}

static void
parse_sequence_no_match (ParsingState         *parsing_state,
			 ParsingStep_Sequence *step)
{
    DEBUG_FLO (
      errf->print ("Pargen.parse_sequence_no_match").pendl ();
    );

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

    if (!step->parser_elements.isEmpty ()) {
	List<ParserElement*>::DataIterator parser_el_iter (step->parser_elements);
	while (!parser_el_iter.done ()) {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_sequence_no_match: accepting element").pendl ();
	    );

	    if (!step->acceptor.isNull ())
		step->acceptor->setParserElement (parser_el_iter.next ());
	}

	DEBUG (
	    errf->print ("Pargen.parse_sequence_no_match: non-empty list").pendl ();
	);
	pop_step (parsing_state, true /* match */, false /* empty_match */);
    } else {
	if (step->optional)
	    pop_step (parsing_state, true /* match */, true /* empty_match */);
	else
	    pop_step (parsing_state, false /* match */, false /* empty_match */);
    }
}

static void
parse_sequence_match (ParsingState         *parsing_state,
		      ParsingStep_Sequence *step)
{
    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

//    Ref<Acceptor> acceptor = grab (static_cast <Acceptor*> (new ListAcceptor<ParserElement> (&step->parser_elements)));
//    VSlab< ListAcceptor<ParserElement> >::Ref * const acceptor = parsing_state->list_acceptor_slab.alloc ();
    VSlabRef< ListAcceptor<ParserElement> > acceptor =
	    VSlabRef< ListAcceptor<ParserElement> >::forRef < ListAcceptor<ParserElement> > (
		    parsing_state->list_acceptor_slab.alloc ());
//    VSlabRef< ListAcceptor<ParserElement> > acceptor (parsing_state->list_acceptor_slab.alloc ());
    acceptor->init (&step->parser_elements);
    for (;;) {
	ParsingResult pres = parse_grammar (parsing_state, step->grammar, acceptor, false /* optional */);
	if (pres == ParseNonemptyMatch)
	    continue;

	if (pres == ParseEmptyMatch ||
	    pres == ParseNoMatch)
	{
	    parse_sequence_no_match (parsing_state, step);
	} else
	    abortIf (pres != ParseUp);

	break;
    }
}

static void
parse_compound_no_match (ParsingState         *parsing_state,
			 ParsingStep_Compound *step)
{
    DEBUG_FLO (
      errf->print ("Pargen.parse_compound_no_match").pendl ();
    );

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

#ifdef PARGEN_UPWARDS_JUMPS
    if (parsing_state->parser_config->upwards_jumps &&
	!step->jump_performed                       &&
	step->got_jump)
    {
	do {
	    if (step->jump_cb != NULL) {
		if (!step->jump_cb (step->parser_element, parsing_state->user_data))
		    break;
	    }

#if 0
	    abortIf (step->jump_grammar->grammar_type != Grammar::t_Switch);
	    Grammar_Switch * const grammar__switch = static_cast <Grammar_Switch*> (step->jump_grammar);

	    push_switch_step (parsing_state,
			      grammar__switch,
			      NULL  /* acceptor */,
			      false /* optional */,
			      step->jump_switch_grammar_entry);
#endif

	    SwitchGrammarEntry * const switch_ge = step->jump_switch_grammar_entry->data;
	    abortIf (switch_ge->grammar->grammar_type != Grammar::t_Compound);
	    Grammar_Compound * const grammar__compound = static_cast <Grammar_Compound*> (switch_ge->grammar.ptr ());

	    push_compound_step (parsing_state,
				grammar__compound,
#ifndef VSLAB_ACCEPTOR
				NULL /* acceptor */,
#else
				VSlabRef<Acceptor> () /* acceptor */,
#endif
				false /* optional */,
				step->got_nonoptional_match,
				step->go_right_count,
				true  /* got_cur_subg_el */,
				step->jump_compound_grammar_entry);

	    step->jump_performed = true;
	    step->go_right_count = 0;

	    DEBUG (
		errf->print ("--- JUMP --- 0x").printHex ((Uint64) step->jump_compound_grammar_entry).pendl ();
	    )

	    return;
	} while (0);
    }
#endif

    if (step->optional) {
	if (step->grammar->accept_func != NULL) {
	    step->grammar->accept_func (NULL,
					parsing_state,
					parsing_state->user_data);
	}

	pop_step (parsing_state, true /* mach */, true /* empty_match */);
    } else {
	pop_step (parsing_state, false /* mach */, false /* empty_match */);
    }
}

static void
parse_compound_match (ParsingState         *parsing_state,
		      ParsingStep_Compound *step,
		      bool                  empty_match)
{
    DEBUG_FLO (
      errf->print ("Pargen.parse_compound_match").pendl ();
    );

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

    if (!empty_match)
	step->got_nonoptional_match = true;

    while (!step->jump_performed &&
	   step->cur_subg_el != NULL)
    {
	CompoundGrammarEntry &entry = step->cur_subg_el->data.der ();
	step->cur_subg_el = step->cur_subg_el->next;

	if (entry.is_jump) {
	    step->got_jump = true;
	    step->jump_grammar = entry.jump_grammar;
	    step->jump_cb = entry.jump_cb;

	    abortIf (entry.jump_grammar->grammar_type != Grammar::t_Switch);
	    Grammar_Switch * const grammar__switch = static_cast <Grammar_Switch*> (entry.jump_grammar);

	    if (entry.jump_switch_grammar_entry == NULL) {
		entry.jump_switch_grammar_entry =
			grammar__switch->grammar_entries.getIthElement (entry.jump_switch_grammar_index);
	    }
	    step->jump_switch_grammar_entry = entry.jump_switch_grammar_entry;

	    SwitchGrammarEntry * const switch_ge = entry.jump_switch_grammar_entry->data;
	    abortIf (switch_ge->grammar->grammar_type != Grammar::t_Compound);
	    Grammar_Compound * const grammar__compound = static_cast <Grammar_Compound*> (switch_ge->grammar.ptr ());

	    if (entry.jump_compound_grammar_entry == NULL &&
		grammar__compound->grammar_entries.getNumElements () != entry.jump_compound_grammar_index)
	    {
		entry.jump_compound_grammar_entry =
			grammar__compound->grammar_entries.getIthElement (entry.jump_compound_grammar_index);
	    }
	    step->jump_compound_grammar_entry = entry.jump_compound_grammar_entry;

	    continue;
	}

	if (entry.inline_match_func != NULL) {
	    DEBUG_CB (
		errf->print ("Pargen.(Parser).parser_compound_match: calling intermediate accept_func()").pendl ();
	    )
	    // Note: This is the only place where inline match functions are called.
// Deprecated	    entry.accept_func (step->parser_element, parsing_state, parsing_state->user_data);
	    if (!entry.inline_match_func (step->parser_element, parsing_state, parsing_state->user_data)) {
	      // Inline match has failed, so we have no match for the current
	      // compound grammar.
		parse_compound_no_match (parsing_state, step);
		return;
	    }

	    continue;
	}

	if (entry.flags & CompoundGrammarEntry::Sequence) {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_compound_match: sequence").pendl ();
	    );

// VSTACK	    Ref<ParsingStep_Sequence> new_step = grab (new ParsingStep_Sequence);
	    VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
	    VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
	    ParsingStep_Sequence * const new_step =
		    new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Sequence))) ParsingStep_Sequence;
	    new_step->vstack_level = tmp_vstack_level;
	    new_step->el_level = tmp_el_level;
	    new_step->acceptor = entry.createAcceptorFor (step->parser_element);
	    new_step->optional = entry.flags & CompoundGrammarEntry::Optional;
	    new_step->grammar = entry.grammar;

// TODO Do not create a new checkpoint for sequence steps, _and_ do not
// commit/cancel the checkpoint in pop_step() accordingly.
//	    push_step (parsing_state, new_step, false /* new_checkpoint */);
	    push_step (parsing_state, new_step);
	    return;
	} else {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_compound_match: not a sequence").pendl ();
	    );

	    {
	    DEBUG_INT (
		errf->print ("Pargen.parse_compound_match: creating acceptor").pendl ();
	    )
	    VSlabRef<Acceptor> acceptor = entry.createAcceptorFor (step->parser_element);
	    DEBUG_INT (
		errf->print ("Pargen.parse_compound_match: acceptor: 0x").printHex ((Uint64) (Acceptor*) acceptor).pendl ();
	    )
	    ParsingResult pres = parse_grammar (parsing_state,
						entry.grammar,
						acceptor,
						entry.flags & CompoundGrammarEntry::Optional);
	    if (pres == ParseNonemptyMatch) {
		step->got_nonoptional_match = true;
		continue;
	    }

	    if (pres == ParseEmptyMatch && (entry.flags & CompoundGrammarEntry::Optional))
		continue;

	    if (pres == ParseNoMatch) {
		parse_compound_no_match (parsing_state, step);
		return;
	    } else
		abortIf (pres != ParseUp);

	    DEBUG_INT (
		errf->print ("Pargen.parse_compound_match: leaving test scope").pendl ();
	    )
	    }
	    DEBUG_INT (
		errf->print ("Pargen.parse_compound_match: left test scope").pendl ();
	    )

	    return;
	}
    }

  // We have parsed all subgrammars.

    bool user_match = true;
// TODO FIXME (explain)
//    if (!empty_match) {
	if (!step->jump_performed &&
	    step->grammar->match_func != NULL)
	{
	    DEBUG_CB (
		errf->print ("Pargen.(Parser).parse_compound_match: calling match_func()").pendl ();
	    )
	    if (!step->grammar->match_func (step->parser_element, parsing_state, parsing_state->user_data))
		user_match = false;
	}
//    }

    if (user_match) {
	DEBUG_INT (
	    errf->print ("Pargen.(Parser).parse_compound_match: match_func() returned true").pendl ();
	)

	if (!step->jump_performed) {
	    if (step->grammar->accept_func != NULL) {
		DEBUG_CB (
		    errf->print ("Pargen.(Parser).parse_compound_match: calling accept_func()").pendl ();
		)
		step->grammar->accept_func (step->parser_element,
					    parsing_state,
					    parsing_state->user_data);
	    }

// TODO FIXME (explain)
//	    if (!empty_match)
	    if (!step->acceptor.isNull ()) {
		DEBUG (
		    errf->print ("parse_compound_match: calling setParserElement(): "
				 "0x").printHex ((Uint64) step->parser_element).pendl ();
		)
		step->acceptor->setParserElement (step->parser_element);
	    }
	}

	DEBUG (
	    errf->print ("parse_compound_match: user_match").pendl ();
	)
	pop_step (parsing_state, true /* mach */, !step->got_nonoptional_match /* empty_match */);
    } else {
	DEBUG_INT (
	    errf->print ("Pargen.(Parser).parse_compound_match: match_func() returned false").pendl ();
	)

	parse_compound_no_match (parsing_state, step);
    }
}

static void
parse_switch_final_match (ParsingState *parsing_state,
			  ParsingStep_Switch *step,
			  bool empty_match)
{
#if 0
// Moved to parse_switch_no_match()
    if (step->grammar->accept_func != NULL) {
	DEBUG_CB (
	    errf->print ("Pargen.(Parser).parse_switch_final_match: calling accept_func()").pendl ();
	)
	step->grammar->accept_func (step->parser_element,
				    parsing_state,
				    parsing_state->user_data);
    }
#endif

// Wrong condition. We should set parser elements for empty matches as well.
// Otherwise, "key = ;" yields NULL 'value' field in mconfig.
//    if (!empty_match) {
	if (!step->acceptor.isNull ()) {
	    DEBUG_INT (
		errf->print ("Pargen.(Parser).parse_switch_final_match: calling setParserElement(): "
			     "0x").printHex ((Uint64) step->parser_element).pendl ();
	    )
	    step->acceptor->setParserElement (step->parser_element);
	}
//    }

    pop_step (parsing_state, true /* match */, empty_match);
}

static void
parse_switch_no_match_yet (ParsingState       *parsing_state,
			   ParsingStep_Switch *step);

static void
parse_switch_match (ParsingState       *parsing_state,
		    ParsingStep_Switch *step,
		    bool                match,
		    bool                empty_match)
{
    abortIf (parsing_state == NULL ||
	     step == NULL          ||
	     (empty_match && !match));

    DEBUG_INT (
      errf->print ("Pargen.parse_switch_match, step->parser_element: "
		   "0x").printHex ((Uint64) step->parser_element).print (", "
		   "step->nlr_parser_element: 0x").printHex ((Uint64) step->nlr_parser_element).pendl ();
    );

    switch (step->state) {
	case ParsingStep_Switch::State_NLR: {
	  // We've just parsed a non-left-recursive grammar.

	  // Note: ParserElement parsed is stored in 'step->nlr_parser_element',
	  // as prescribed in the acceptor that we set for NLR grammars.

	    if (empty_match) {
	      // We've got an empty match. For recursion handling, we want to see
	      // if there'll be any non-empty matches for the switch. Empty matches
	      // can't be used for parsing left-recursive grammars, so we move on
	      // to the next non-left-recursive subgrammar.

		step->got_empty_nlr_match = true;

		DEBUG_INT (
		    errf->print ("Pargen.(Parser).parse_switch_match (NLR, empty): "
				 "calling parse_switch_no_match_yet()").pendl ();
		)
		parse_switch_no_match_yet (parsing_state, step);
	    } else {
	      // We've got a non-empty match. Let's try parsing left-recursive grammars,
	      // if any, with this match at the left.

		if (match) {
		    if (step->grammar->accept_func != NULL) {
			DEBUG_CB (
			    errf->print ("Pargen.(Parser).parse_switch_match: "
					 "calling accept_func (NLR, non-empty)").pendl ();
			)
			step->grammar->accept_func (step->nlr_parser_element,
						    parsing_state,
						    parsing_state->user_data);
		    }

		    step->got_nonempty_nlr_match = true;
		}

		step->state = ParsingStep_Switch::State_LR;
		step->cur_lr_el = static_cast <Grammar_Switch*> (step->grammar)->grammar_entries.first;

		DEBUG_INT (
		    errf->print ("Pargen.(Parser).parse_switch_match (NLR, non-empty): "
				 "calling parse_switch_no_match_yet()").pendl ();
		)
		parse_switch_no_match_yet (parsing_state, step);
	    }
	} break;
	case ParsingStep_Switch::State_LR: {
	  // We've been parsing left-recursive grammars.

	    if (empty_match) {
	      // The match is empty. This means that the recursive grammar
	      // has an empty tail, hence the match is not recursive at all.
	      // Moving on to the next left-recursive grammar.

		DEBUG_INT (
		    errf->print ("Pargen.(Parser).parse_switch_match (LR, empty): "
				 "calling parse_switch_no_match_yet()").pendl ();
		)
		parse_switch_no_match_yet (parsing_state, step);
		return;
	    }

	    // Note: when we're building a long recursive chain of elements,
	    // each intermediate state of the chain should be a match.
	    // It's not clear at this point if we'll have to call match_func()
	    // on each iteration.

	    // Here we're making a trick: pretending like we've just parsed
	    // a non-left-recursive grammar, so that we can move on with
	    // the recursion.

	    // Calling accept_func early.
	    if (step->grammar->accept_func != NULL) {
		DEBUG_CB (
		    errf->print ("Pargen.(Parser).parse_switch_match: calling accept_func").pendl ();
		)
		step->grammar->accept_func (step->parser_element,
					    parsing_state,
					    parsing_state->user_data);
	    }

	    step->got_lr_match = true;

	    step->nlr_parser_element = step->parser_element;
	    step->parser_element = NULL;
	    step->cur_lr_el = static_cast <Grammar_Switch*> (step->grammar)->grammar_entries.first;

	    parse_switch_no_match_yet (parsing_state, step);

// TODO Figure out how to call match_func() properly for left-recursive grammars.
// The net result should be equivalent to what happens when dealing with othre
// type of recursion.
#if 0
	    if (step->grammar->match_func != NULL) {
		DEBUG_CB (
		    errf->print ("Pargen.(Parser).parse_switch_match: calling match_func()").pendl ();
		)
		if (!step->grammar->match_func (step->parser_element, parsing_state->user_data)) {
		    parse_switch_no_match_yet (parsing_state, step);
		    return;
		}
	    }
#endif
	} break;
	default:
	   abortIfReached ();
    }
}

static bool
is_cur_variant (ParsingState * const parsing_state,
		SwitchGrammarEntry const &entry)
{
    if (entry.variants.isEmpty ())
	return true;

    List< Ref<String> >::DataIterator variant_iter (entry.variants);
    while (!variant_iter.done ()) {
	Ref<String> &variant = variant_iter.next ();
	if (parsing_state->variant.isNull () ||
	    parsing_state->variant->isNullString ())
	{
//	    errf->print ("--- VARIANT ").print (variant).print (" <-> ").print (parsing_state->default_variant).pendl ();
	    if (compareByteArrays (variant->getMemoryDesc (),
				   parsing_state->default_variant)
			== ComparisonEqual)
	    {
		return true;
	    }
	} else {
//	    errf->print ("--- VARIANT ").print (variant).print (" <=> ").print (parsing_state->variant).pendl ();
	    if (compareStrings (variant->getData (), parsing_state->variant->getData ()))
		return true;
	}
    }

    return false;
}

// Upwards optimization.
static bool
parse_switch_upwards_green_forward (ParsingState       * const parsing_state        /* non-null */,
				    SwitchGrammarEntry * const switch_grammar_entry /* non-null */)
{
#ifdef PARGEN_FORWARD_OPTIMIZATION
  // Note: this is a raw non-optimized version.
  //     * Uses linked list traversal instead of map/hash lookups;
  //     * The list of tranzition entries is not cleaned up.
  //       It contains overlapping entries (like "any token" at the end
  //       of the list).

    if (switch_grammar_entry->any_tranzition)
	return true;

    ConstMemoryDesc token;
    Ref<SimplyReferenced> user_obj;
    void *user_ptr;
    {
	TokenStream::PositionMarker pmark;
	parsing_state->token_stream->getPosition (&pmark);
	token = parsing_state->token_stream->getNextToken (&user_obj, &user_ptr);
	parsing_state->token_stream->setPosition (&pmark);
    }

    if (token.getLength () == 0)
	return false;

    {
	List< Ref<TranzitionMatchEntry> >::DataIterator iter (
		switch_grammar_entry->tranzition_match_entries);
	while (!iter.done ()) {
	    Ref<TranzitionMatchEntry> &tranzition_match_entry = iter.next ();

	    DEBUG_OPT2 (
		errf->print ("--- TOKEN MATCH CB").pendl ();
	    )

	    if (tranzition_match_entry->token_match_cb (token,
							user_ptr,
							parsing_state->user_data))
	    {
//		errf->print ("Ut\n");
		return true;
	    }
	}
    }

#if 0
    {
	List< Ref<TranzitionEntry> >::DataIterator iter (
		switch_grammar_entry->tranzition_entries);
	while (!iter.done ()) {
	    Ref<TranzitionEntry> &tranzition_entry = iter.next ();

#if 0
// Deprecated
	    if (tranzition_entry->token.isNull () ||
		tranzition_entry->token->getLength () == 0)
	    {
//		errf->print ("Un\n");
		return true;
	    }
#endif

	    if (compareByteArrays (token,
				   tranzition_entry->token->getMemoryDesc ())
			== ComparisonEqual)
	    {
//		errf->print ("Uc\n");
		return true;
	    }
	}
    }
#endif

    /* FIXME getMemory() does not return a zero-terminated string */
    DEBUG_OPT2 (
	errf->print ("--- FIND: ").print ((char const*) token.getMemory ()).pendl ();
    )
//    if (switch_grammar_entry->tranzition_entries.find ((char const*) token.getMemory ()) != switch_grammar_entry->tranzition_entries.end ()) {
    if (switch_grammar_entry->tranzition_entries.lookup (token)) {
//	errf->print ("Uc\n");
	return true;
    }

//    errf->print ("!U\n");
    return false;
#else
    return true;
#endif // PARGEN_FORWARD_OPTIMIZATION
}

static bool
parse_switch_upwards_green (ParsingState       * const parsing_state        /* non-null */,
			    SwitchGrammarEntry * const switch_grammar_entry /* non-null */)
{
  FUNC_NAME (
    char const * const _func_name = "Pargen.Parser.parse_switch_upwards_green";
  )

#ifdef PARGEN_NEGATIVE_CACHE
    if (parsing_state->negative_cache.isNegative (switch_grammar_entry->grammar)) {
	DEBUG_NEGC (
	    errf->print (_func_name).print (": negative ").print (switch_grammar_entry->grammar->toString ()).pendl ();
	)

//	if (parsing_state->debug_dump)
//	    errf->print ("!");

	return false;
    }
#endif

    if (switch_grammar_entry->grammar->optimized)
	return parse_switch_upwards_green_forward (parsing_state, switch_grammar_entry);

    return true;
}

static void
parse_switch_no_match_yet (ParsingState       * const parsing_state,
			   ParsingStep_Switch * const step)
{
  DEBUG_FLO (
    static char const * const _func_name = "Pargen.Parser.parse_switch_no_match_yet";
  )

    DEBUG_FLO (
      errf->print (_func_name).pendl ();
    )

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

    // Here we're going to deal with left recursion.
    //
    // The general case for left recursion is the following:
    //
    //     a:
    //         b_opt a c
    //
    // This grammar turns out to be left-recursive if b_opt evalutes
    // to an empty sequence. In this case, we get "a: a c", which can't
    // be processed by out left-to-right parser without special treatment.
    //
    // We're going to deal with this by delaying parsing of left-recursive
    // grammars until any non-left-recursive grammar evaluates
    // to a non-empty sequence.
    //
    // In general, any left-recursive grammar can potentially be parsed
    // only if it is a part of a switch-type parent with at least one
    // non-left-recursive child. Once we've got a match for such a child,
    // we can move forward with parsing the recursive ones, throwing away
    // recursive references at their left. We can do this by simulating
    // a match for the first element of the corresponding compound grammars.
    //
    // Left-recursive grammars are always a better match than non-left-recursive
    // ones.
    //
    // We'll parse the following grammar in two steps:
    //
    //     a:
    //         b
    //         d
    //         a c
    //         a e
    //
    //     1. Parse switch { "b", "d" }, choose the one that matches
    //        (say, "b"), remember it.
    //     2. Parse switch { "a c", "a e" }, with "a" pre-matched as "b".
    //        If we've got a match, then that's the result. If not, then
    //        the result is the remembered "b".
    //
    // If a left-recursive grammar's match sequence is not longer than
    // the remembered one the remember non-left-recursive grammar
    //
    // Note that multiple occurences of references to the parent grammar
    // is not a special case. As long as we've got a match for the leftmost
    // reference, the rest of the grammar is not left-recursive anymore,
    // and we can safely parse it as usual.

    switch (step->state) {
	case ParsingStep_Switch::State_NLR: {
	  // At this step we're parsing non-left-recursive grammars only.

	    DEBUG (
		errf->print ("Pargen.parse_switch_no_match_yet: NLR").pendl ();
	    )

	    // Workaround for the unfortunate side effect of acceptor initialization:
	    // it nullifies the target pointer.
	    ParserElement *tmp_nlr_parser_element = step->nlr_parser_element;

#ifndef VSLAB_ACCEPTOR
	    Ref<Acceptor> nlr_acceptor =
		    grab (static_cast <Acceptor*> (new RefAcceptor<ParserElement> (&step->nlr_parser_element)));
#else
	    VSlabRef< PtrAcceptor<ParserElement> > nlr_acceptor =
		    VSlabRef< PtrAcceptor<ParserElement> >::forRef < PtrAcceptor<ParserElement> > (
			    parsing_state->ptr_acceptor_slab.alloc ());
	    nlr_acceptor->init (&step->nlr_parser_element);
	    DEBUG_INT (
		errf->print ("Pargen.parse_switch_no_match_yet: NLR: acceptor: "
			     "0x").printHex ((Uint64) (Acceptor*) nlr_acceptor).pendl ();
	    )
#endif

	    step->nlr_parser_element = tmp_nlr_parser_element;

	    bool got_new_step = false;
	    while (step->cur_nlr_el != NULL) {
		DEBUG (
		    errf->print ("Pargen.parse_switch_no_match_yet: NLR: iteration").pendl ();
		)

		SwitchGrammarEntry &entry = step->cur_nlr_el->data.der ();
		step->cur_nlr_el = step->cur_nlr_el->next;

		if (!is_cur_variant (parsing_state, entry))
		    continue;

		if (!parse_switch_upwards_green (parsing_state, &entry))
		    continue;

		// I suppose that the better option is to give up on detecting left-recursive
		// grammars for the cases where subgrammar is not a compound one. For pargen-generated
		// grammars this means we're never giving up :)
		if (entry.grammar->grammar_type == Grammar::t_Compound) {
		    DEBUG (
			errf->print ("Pargen.parse_switch_no_match_yet: NLR: _Compound").pendl ();
		    )

		    Grammar_Compound *grammar = static_cast <Grammar_Compound*> (entry.grammar.ptr ());

		    {
			Grammar * const first_subg = grammar->getFirstSubgrammar ();
			if (first_subg == step->grammar)
			{
			  // The subgrammar happens to be a left-recursive one.
			  // We're simply proceeding to the next subgrammar.

			    continue;
			}
		    }

// VSTACK		    Ref<ParsingStep_Compound> compound_step = grab (new ParsingStep_Compound);
		    VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
		    VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
		    ParsingStep_Compound * const compound_step =
			    new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Compound))) ParsingStep_Compound;
		    compound_step->vstack_level = tmp_vstack_level;
		    compound_step->el_level = tmp_el_level;
		    compound_step->lr_parent = step->grammar;
		    compound_step->acceptor = nlr_acceptor;
		    compound_step->optional = false;
		    compound_step->grammar = grammar;
		    compound_step->cur_subg_el = grammar->grammar_entries.first;
		    compound_step->parser_element = grammar->createParserElement (parsing_state->el_vstack);
		    push_step (parsing_state, compound_step);

//		    errf->print (_func_name).print (": new parser_element: 0x").printHex ((Uint64) compound_step->parser_element).pendl ();
		} else {
		    DEBUG (
			errf->print ("Pargen.parse_switch_no_match_yet: NLR: non-compound").pendl ();
		    )

		    // TEST
		    abortIfReached ();

		    // Note: this path is currently inadequate.
#if 0
		    ParsingResult pres = parse_grammar (parsing_state, entry.grammar, nlr_acceptor, false /* optional */);
		    if (pres == ParseEmptyMatch ||
			pres == ParseNoMatch)
		    {
			continue;
		    }

		    if (pres == ParseNonemptyMatch) {
			step->parser_element = step->nlr_parser_element;
			// TODO Call match_func here (helps to avoid recursion)
			parse_switch_match (parsing_state, step, true /* match */, false /* empty_match */);
		    } else
			abortIf (pres != ParseUp);
#endif
		}

		got_new_step = true;
		break;
	    }

	    if (!got_new_step) {
		if (step->got_empty_nlr_match) {
		  // We've got an empty match. It cannot be used for handling left-recursive grammars,
		  // so we just accept it.

		    DEBUG_INT (
			errf->print ("Pargen.(Parser).parse_switch_no_match_yet: "
				     "empty NLR match, step->nlr_parser_element: "
				     "0x").printHex ((Uint64) step->nlr_parser_element).pendl ();
		    )
		    step->parser_element = step->nlr_parser_element;

		    if (step->grammar->accept_func != NULL) {
			DEBUG_CB (
			    errf->print ("Pargen.(Parser).parse_switch_no_match_yet: calling accept_func()").pendl ();
			)
			step->grammar->accept_func (step->parser_element,
						    parsing_state,
						    parsing_state->user_data);
		    }

		    parse_switch_final_match (parsing_state, step, true /* empty_match */);
		} else {
		  // We've tried all of the Switch grammar's non-left-recursive subgrammars,
		  // and none of them match.

		  // Still, if there are lef-recursive subgrammars with optional left-recursive part,
		  // then there might be a match for the current switch grammar.
		  // Proceeding to left-recursive subgrammars handling.

		  // Note: This code path and functions' naming is counterintuitive.
		  // It would make sense to sort this out.

		    parse_switch_match (parsing_state, step, false /* match */, false /* empty_match */);
		}
	    }
	} break;
	case ParsingStep_Switch::State_LR: {
	  // We're parsing only left-recursive grammars now.

	    DEBUG (
		errf->print ("Pargen.parse_switch_no_match_yet: LR").pendl ();
	    )

#ifndef VSLAB_ACCEPTOR
	    Ref<Acceptor> lr_acceptor =
		    grab (static_cast <Acceptor*> (new RefAcceptor<ParserElement> (&step->parser_element)));
#else
	    VSlabRef< PtrAcceptor<ParserElement> > lr_acceptor =
		    VSlabRef< PtrAcceptor<ParserElement> >::forRef < PtrAcceptor<ParserElement> > (
			    parsing_state->ptr_acceptor_slab.alloc ());
	    lr_acceptor->init (&step->parser_element);
	    DEBUG_INT (
		errf->print ("Pargen.parse_switch_no_match_yet: "
			     "LR: acceptor: 0x").printHex ((Uint64) (Acceptor*) lr_acceptor).pendl ();
	    )
#endif

	    bool got_new_step = false;
	    while (step->cur_lr_el != NULL) {
		DEBUG (
		    errf->print ("Pargen.parse_switch_no_match_yet: LR: iteration").pendl ();
		)

		SwitchGrammarEntry &entry = step->cur_lr_el->data.der ();
		step->cur_lr_el = step->cur_lr_el->next;

		if (entry.grammar->grammar_type != Grammar::t_Compound)
		    continue;

		if (!is_cur_variant (parsing_state, entry))
		    continue;

	      // Note: checking negative cache here is pointless, since it will
	      // be checked in parse_up() anyway.

		Grammar_Compound *grammar = static_cast <Grammar_Compound*> (entry.grammar.ptr ());

		{
		  // Checking if the grammar is left-recursive and suitable.

		    CompoundGrammarEntry * const first_subg = grammar->getFirstSubgrammarEntry ();
		    if (first_subg == NULL ||
			(first_subg->grammar != step->grammar))
		    {
		      // The subgrammar is not a left-recursive one.
		      // Proceeding to the next subgrammar.

			DEBUG (
			    errf->print ("Pargen.parse_switch_no_match_yet: LR: non-lr").pendl ();
			)

			continue;
		    }

		    if (!step->got_empty_nlr_match    &&
			!step->got_nonempty_nlr_match &&
			!(first_subg->flags & CompoundGrammarEntry::Optional))
		    {
		      // If we've got an empty nlr match, then only left-recursive grammars
		      // with optional left-recursive part can match.

			continue;
		    }
		}

		got_new_step = true;

// VSTACK		Ref<ParsingStep_Compound> compound_step = grab (new ParsingStep_Compound);
		VStack::Level const tmp_vstack_level = parsing_state->step_vstack.getLevel ();
		VStack::Level const tmp_el_level = parsing_state->el_vstack->getLevel ();
		ParsingStep_Compound * const compound_step =
			new (parsing_state->step_vstack.push_malign (sizeof (ParsingStep_Compound))) ParsingStep_Compound;
		compound_step->vstack_level = tmp_vstack_level;
		compound_step->el_level = tmp_el_level;
		compound_step->lr_parent = step->grammar;
		compound_step->acceptor = lr_acceptor;
		compound_step->optional = false;
		compound_step->grammar = grammar;
		// We'll start parsing from the second subgrammar (the first one is
		// a left-recursive reference to the parent grammar).
		compound_step->cur_subg_el = grammar->getSecondSubgrammarElement ();
		compound_step->parser_element = grammar->createParserElement (parsing_state->el_vstack);

//#if 0
// TODO This breaks normal operation. Look at this carefully.
// Deprecated as long as we don't call leading inline accept callbacks.

		// Note: This is a hack: we create the checkpoint early to be able
		// to call inline accept functions for match simulation.
		if (!parsing_state->lookup_data.isNull ())
		    parsing_state->lookup_data->newCheckpoint ();
//#endif

		{
		  // We're simulating a match. Inline accept callbacks which stand before
		  // the left-recursive subgrammar must be called.

		  // Note: It looks like calling these accept callbacks is confusing and does no good.

		    List< Ref<CompoundGrammarEntry> >::DataIterator iter (grammar->grammar_entries);
		    while (!iter.done ()) {
			Ref<CompoundGrammarEntry> &entry = iter.next ();
			if (entry->inline_match_func == NULL)
			    break;

			// TODO Aborting is a temporal measure.
			//
			// [10.09.15] Not so temporal anymore. I think this should become
			// a permanent ban.
			errf->print ("Leading inline accept callbacks in left-recursive grammars "
				     "are never called").pendl ();
			abortIfReached ();

#if 0
// Deprecated
			entry->accept_func (compound_step->parser_element, parsing_state, parsing_state->user_data);
#endif
		    }
		}

		if (step->nlr_parser_element != NULL) {
		  // Pre-setting the compound grammar's first subgrammar with
		  // the remembered non-left-recursive match.

		    CompoundGrammarEntry * const cg_entry = grammar->getFirstSubgrammarEntry ();
		    abortIf (cg_entry == NULL);

		    if (cg_entry->assignment_func != NULL)
			cg_entry->assignment_func (compound_step->parser_element, step->nlr_parser_element);
		}

		push_step (parsing_state, compound_step, false /* new_checkpoint */);

//		errf->print (_func_name).print (": new parser_element: 0x").printHex ((Uint64) compound_step->parser_element).pendl ();
		break;
	    }

	    if (!got_new_step) {
	      // None of left-recursive subgrammars match. In this case,
	      // the remembered non-left-recursive match is the result.

		DEBUG (
		    errf->print ("Pargen.parse_switch_no_match_yet: LR: none match").pendl ();
		)

		step->parser_element = step->nlr_parser_element;

		if (step->got_nonempty_nlr_match &&
		    step->grammar->match_func != NULL)
		{
		    DEBUG_CB (
			errf->print ("Pargen.(Parser).parse_switch_no_match_yet: calling match_func()").pendl ();
		    )
		    if (!step->grammar->match_func (step->parser_element, parsing_state, parsing_state->user_data)) {
			pop_step (parsing_state, false /* match */, false /* empty_match */);
			return;
		    }
		}

		if (step->got_lr_match) {
		  // We've got a left-recursive match.

		    parse_switch_final_match (parsing_state, step, false /* empty_match */);
		} else {
		    if (!step->got_nonempty_nlr_match && !step->got_empty_nlr_match) {
			if (step->optional) {
			    if (step->grammar->accept_func != NULL) {
				DEBUG_CB (
				    errf->print ("Pargen.(Parser).parse_switch_no_match_yet: calling accept_func(NULL)").pendl ();
				)
				step->grammar->accept_func (NULL,
							    parsing_state,
							    parsing_state->user_data);
			    }

			    pop_step (parsing_state, true /* match */, true /* empty_match */);
			} else
			    pop_step (parsing_state, false /* match */, false /* empty_match */);
		    } else {
			if (step->got_empty_nlr_match) {
			    if (step->grammar->accept_func != NULL) {
				DEBUG_CB (
				    errf->print ("Pargen.(Parser).parse_switch_no_match_yet: calling accept_func(NULL)").pendl ();
				)
				step->grammar->accept_func (NULL,
							    parsing_state,
							    parsing_state->user_data);
			    }

			    parse_switch_final_match (parsing_state, step, true /* empty_match */);
			} else
			    parse_switch_final_match (parsing_state, step, false /* empty_match */);
		    }
		}
	    }
	} break;
	default:
	    abortIfReached ();
    }

    DEBUG (
	errf->print ("Pargen.parse_switch_no_match_yet: done").pendl ();
    )
}

static void
parse_alias (ParsingState      *parsing_state,
	     ParsingStep_Alias *step)
{
  FUNC_NAME (
    static char const * const _func_name = "Pargen.Parser.parse_alias";
  )

    DEBUG_FLO (
	errf->print (_func_name).pendl ();
    )

    abortIf (parsing_state == NULL);
    abortIf (step == NULL);

//    Ref< RefAcceptor<ParserElement> > acceptor = grab (new RefAcceptor<ParserElement> (&step->parser_element));
    VSlabRef< PtrAcceptor<ParserElement> > acceptor =
	    VSlabRef< PtrAcceptor<ParserElement> >::forRef < PtrAcceptor<ParserElement> > (
		    parsing_state->ptr_acceptor_slab.alloc ());
    acceptor->init (&step->parser_element);

    ParsingResult pres = parse_grammar (parsing_state,
					static_cast <Grammar_Alias*> (step->grammar)->aliased_grammar,
					acceptor,
					step->optional);
    if (pres == ParseUp)
	return;

    switch (pres) {
	case ParseNonemptyMatch:
	    pop_step (parsing_state, true /* match */, false /* empty_match */);
	    break;
	case ParseEmptyMatch:
	    pop_step (parsing_state, true /* match */, true /* empty_match */);
	    break;
	case ParseNoMatch:
	    DEBUG_INT (
		errf->print (_func_name).print (": pop_step: false, false").pendl ();
	    )
	    pop_step (parsing_state, false /* match */, false /* empty_match */);
	    break;
	default:
	    abortIfReached ();
    }
}

static void
parse_up (ParsingState *parsing_state)
{
  FUNC_NAME (
    static char const * const _func_name = "Pargen.Parser.parse_up";
  )

    abortIf (parsing_state == NULL);

    ParsingStep &_step = parsing_state->getLastStep ();

    switch (_step.parsing_step_type) {
	case ParsingStep::t_Sequence: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": ParsingStep::_Sequence").pendl ();
	    );
	    ParsingStep_Sequence &step = static_cast <ParsingStep_Sequence&> (_step);

	    parse_sequence_match (parsing_state, &step);
	} break;
	case ParsingStep::t_Compound: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": ParsingStep::_Compound").pendl ();
	    );

	    ParsingStep_Compound &step = static_cast <ParsingStep_Compound&> (_step);

	    if (static_cast <Grammar_Compound*> (step.grammar)->getFirstSubgrammar () == step.grammar) {
	      // FIXME 1) This is now allowed;
	      //       2) This assertion should be hit for lr grammars.
	      //
	      // The compound grammar happens to be left-recursive. We do not support that.
		abortIfReached ();
	    }

	    if (step.grammar->begin_func != NULL)
		step.grammar->begin_func (parsing_state->user_data);

	    parse_compound_match (parsing_state, &step, true /* empty_match */);
	} break;
	case ParsingStep::t_Switch: {
	    DEBUG_INT (
	      errf->print (_func_name).print (": ParsingStep::_Switch").pendl ();
	    );

	    ParsingStep_Switch &step = static_cast <ParsingStep_Switch&> (_step);

	    if (step.grammar->begin_func != NULL)
		step.grammar->begin_func (parsing_state->user_data);

	    parse_switch_no_match_yet (parsing_state, &step);
	} break;
	case ParsingStep::t_Alias: {
	    DEBUG_INT (
		errf->print (_func_name).print (": ParsingStep::_Alias").pendl ();
	    )

	    ParsingStep_Alias &step = static_cast <ParsingStep_Alias&> (_step);

	    if (step.grammar->begin_func != NULL)
		step.grammar->begin_func (parsing_state->user_data);

	    parse_alias (parsing_state, &step);
	} break;
	default:
	    abortIfReached ();
    };
}

static void
parse_down (ParsingState *parsing_state)
{
    abortIf (parsing_state == NULL);

    ParsingStep &_step = parsing_state->getLastStep ();

    switch (_step.parsing_step_type) {
	case ParsingStep::t_Sequence: {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_down: ParsingStep::_Sequence").pendl ();
	    );

	    ParsingStep_Sequence &step = static_cast <ParsingStep_Sequence&> (_step);

	    if (parsing_state->match)
		parse_sequence_match (parsing_state, &step);
	    else
		parse_sequence_no_match (parsing_state, &step);
	} break;
	case ParsingStep::t_Compound: {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_down: ParsingStep::_Compound").pendl ();
	    );

	    ParsingStep_Compound &step = static_cast <ParsingStep_Compound&> (_step);

	    if (parsing_state->match)
		parse_compound_match (parsing_state, &step, parsing_state->empty_match);
	    else
		parse_compound_no_match (parsing_state, &step);
	} break;
	case ParsingStep::t_Switch: {
	    DEBUG_INT (
	      errf->print ("Pargen.parse_down: ParsingStep::_Switch").pendl ();
	    );

	    ParsingStep_Switch &step = static_cast <ParsingStep_Switch&> (_step);

	    if (parsing_state->match)
		parse_switch_match (parsing_state, &step, true /* match */, parsing_state->empty_match);
	    else
		parse_switch_no_match_yet (parsing_state, &step);
	} break;
	case ParsingStep::t_Alias: {
	    DEBUG_INT (
		errf->print ("Pargen.parse_down: ParsingStep::_Alias").pendl ();
	    )

	    ParsingStep_Alias &step = static_cast <ParsingStep_Alias&> (_step);

	    if (parsing_state->match && !parsing_state->empty_match) {
		bool user_match = true;
		if (step.grammar->match_func != NULL) {
		    DEBUG_CB (
			errf->print ("Pargen.parse_down: calling match_func()").pendl ();
		    )
		    parsing_state->position_changed = false;
		    if (!step.grammar->match_func (step.parser_element, parsing_state, parsing_state->user_data))
			user_match = false;

		    if (parsing_state->position_changed)
			return;
		}

		if (user_match) {
		    if (step.grammar->accept_func != NULL)
			step.grammar->accept_func (step.parser_element, parsing_state, parsing_state->user_data);

		    // This is a non-empty match case.
//		    abortIf (step.parser_element.isNull ());
		    if (!step.acceptor.isNull ())
			step.acceptor->setParserElement (step.parser_element);

		    pop_step (parsing_state, true /* match */, false /* empty_match */);
		} else {
		  // FIXME Code duplication (see right below)

		    if (_step.optional) {
		      // This is an empty match case.

			if (step.grammar->accept_func != NULL)
			    step.grammar->accept_func (NULL, parsing_state, parsing_state->user_data);

			pop_step (parsing_state, true /* match */, true /* empty_match */);
		    } else {
			pop_step (parsing_state, false /* match */, false /* empty_match */);
		    }
		}
	    } else {
		if (_step.optional) {
		    // This is an empty match case.
		    abortIf (step.parser_element != NULL);
		    if (step.grammar->accept_func != NULL)
			step.grammar->accept_func (NULL, parsing_state, parsing_state->user_data);

		    pop_step (parsing_state, true /* match */, true /* empty_match */);
		} else {
		    pop_step (parsing_state, false /* match */, false /* empty_match */);
		}
	    }
	} break;
	default:
	    abortIfReached ();
    }
}

#if 0
// Original version

// If 'tranzition_entries' is NULL, then we're in the process of
// iterating the nodes of the grammar.
//
// If 'tranzition_entries' is non-null, then fills 'tranzition_entries'
// with possible tranzitions for 'grammar'.
//
// If 'tranzition_entries' is non-null and the path is fully optional,
// then 'ret_optional' is set to true on return. Otherwise, 'ret_optional'
// is set to false. If 'tranzition_entries' is null, then the value
// of 'ret_optional' after return is undefined. 'ret_optional' may be null.
//
// Returns 'true' if a tranzition has been recorded for the current path.
// If 'tranzition_entries' is null, then return value is undefined.
//
// TODO Separate 'tranzition_entries' filling from initial walkthrough.
//
static bool
do_optimizeGrammar (Grammar                      * const grammar            /* non-null */,
		    List< Ref<TranzitionEntry> > * const tranzition_entries /* non-null */,
		    bool                         * const ret_optional,
		    Size                         * const loop_id            /* non-null */)
{
    if (ret_optional != NULL)
	*ret_optional = false;

    if (tranzition_entries == NULL) {
	if (grammar->optimized)
	    return false;

	grammar->optimized = true;
    } else {
	if (grammar->loop_id == *loop_id) {
	    if (ret_optional != NULL)
		*ret_optional = true;

	    return false;
	}

	grammar->loop_id = *loop_id;
    }

    switch (grammar->grammar_type) {
	case Grammar::t_Immediate: {
	    Grammar_Immediate_SingleToken * const grammar__immediate =
		    static_cast <Grammar_Immediate_SingleToken*> (grammar);

	    if (tranzition_entries != NULL) {
		if (grammar__immediate->getToken ().isNull () ||
		    grammar__immediate->getToken ()->getLength () == 0)
		{
		    if (!grammar__immediate->token_match_cb_name.isNull ()) {
			DEBUG_OPT (
			    errf->print ("  {").print (grammar__immediate->token_match_cb_name).print ("}");
			)
		    } else {
			abortIf (grammar__immediate->token_match_cb != NULL);
			DEBUG_OPT (
			    errf->print (" ANY");
			)
		    }
		} else {
		    DEBUG_OPT (
			errf->print (" ").print (grammar__immediate->getToken ()).print ("");
		    )
		}

		Ref<TranzitionEntry> tranzition_entry = grab (new TranzitionEntry);
		tranzition_entry->token = grammar__immediate->getToken ();
		tranzition_entry->token_match_cb = grammar__immediate->token_match_cb;
		tranzition_entries->append (tranzition_entry);
	    }

	    return true;
	} break;
	case Grammar::t_Compound: {
	    Grammar_Compound * const grammar__compound =
		    static_cast <Grammar_Compound*> (grammar);

	    Bool got_tranzition = false;
	    Bool optional = true;
	    List< Ref<CompoundGrammarEntry> >::DataIterator iter (grammar__compound->grammar_entries);
	    while (!iter.done ()) {
		Ref<CompoundGrammarEntry> &compound_grammar_entry = iter.next ();

		if (compound_grammar_entry->grammar.isNull ()) {
		  // _AcceptCb or _UniversalAcceptCb or UpwardsAnchor.
		    continue;
		}

		if (tranzition_entries != NULL) {
		    bool tmp_optional = false;
		    got_tranzition = do_optimizeGrammar (compound_grammar_entry->grammar,
							 tranzition_entries,
							 &tmp_optional,
							 loop_id);
		    if (!tmp_optional &&
			!(compound_grammar_entry->flags & CompoundGrammarEntry::Optional))
		    {
//			errf->print ("Z");
			optional = false;
		    }

		    if (!optional) {
//			errf->print ("X");
			break;
		    }
		} else {
		    do_optimizeGrammar (compound_grammar_entry->grammar,
					NULL /* tranzition_entries */,
					NULL /* ret_optional */,
					loop_id);
		}
	    }

	    if (ret_optional != NULL)
		*ret_optional = optional;

	    return got_tranzition;
	} break;
	case Grammar::t_Switch: {
	    Grammar_Switch * const grammar__switch =
		    static_cast <Grammar_Switch*> (grammar);

	    Bool got_tranzition = false;
	    Bool optional = true;
	    List< Ref<SwitchGrammarEntry> >::DataIterator iter (grammar__switch->grammar_entries);
	    while (!iter.done ()) {
		Ref<SwitchGrammarEntry> &switch_grammar_entry = iter.next ();

		if (tranzition_entries != NULL) {
		    bool tmp_optional = false;
		    got_tranzition = do_optimizeGrammar (switch_grammar_entry->grammar,
							 tranzition_entries,
							 &tmp_optional,
							 loop_id);
		    if (!tmp_optional &&
			!(switch_grammar_entry->flags & CompoundGrammarEntry::Optional))
		    {
			optional = false;
		    }
		} else {
		    DEBUG_OPT (
			errf->print ("").print (switch_grammar_entry->grammar->toString ()).print (": ");
		    )
		    {
			bool tmp_optional = false;
			do_optimizeGrammar (switch_grammar_entry->grammar,
					    &switch_grammar_entry->tranzition_entries,
					    &tmp_optional,
					    loop_id);
			if (tmp_optional) {
			  // Fully optional grammars should not be upwards-optimized.
			  // We add 'any' token to force entering.
			    switch_grammar_entry->tranzition_entries.append (grab (new TranzitionEntry));
			}
		    }
		    DEBUG_OPT (
			errf->print ("\n");
		    )

		    abortIf (*loop_id + 1 <= *loop_id);
		    (*loop_id) ++;
		    do_optimizeGrammar (switch_grammar_entry->grammar,
					NULL /* tranzition_entries */,
					NULL /* ret_optional */,
					loop_id);
		}
	    }

	    if (ret_optional != NULL)
		*ret_optional = optional;

	    return got_tranzition;
	} break;
	case Grammar::t_Alias: {
	    Grammar_Alias * const grammar_alias =
		    static_cast <Grammar_Alias*> (grammar);

	    return do_optimizeGrammar (grammar_alias->aliased_grammar,
				       tranzition_entries,
				       ret_optional,
				       loop_id);
	} break;
	default:
	    abortIfReached ();
    }

    // Unreachable
    abortIfReached ();
    return false;
}
#endif

// If 'tranzition_entries' is NULL, then we're in the process of
// iterating the nodes of the grammar.
//
// If 'tranzition_entries' is non-null, then do_optimizeGrammar() fills
// 'tranzition_entries' with possible tranzitions for 'grammar'.
//
// If 'tranzition_entries' is non-null and the path is fully optional,
// then 'ret_optional' is set to true on return. Otherwise, 'ret_optional'
// is set to false. If 'tranzition_entries' is null, then the value
// of 'ret_optional' after return is undefined. 'ret_optional' may be null.
//
// Returns 'true' if a tranzition has been recorded for the current path.
// If 'tranzition_entries' is null, then return value is undefined.
//
// TODO Separate 'tranzition_entries' filling from initial walkthrough.
//
static bool
do_optimizeGrammar (Grammar                               * const grammar            /* non-null */,
//		    std::hash_set< Ref<TranzitionEntry> > * const tranzition_entries /* non-null */,
		    SwitchGrammarEntry::TranzitionEntryHash * const tranzition_entries,
		    List< Ref<TranzitionMatchEntry> > * const tranzition_match_entries,
		    SwitchGrammarEntry                * const param_switch_grammar_entry,
		    bool                                  * const ret_optional,
		    Size                                  * const loop_id            /* non-null */)
{
    if (ret_optional != NULL)
	*ret_optional = false;

    if (tranzition_entries == NULL) {
	if (grammar->optimized)
	    return false;

	grammar->optimized = true;
    } else {
	if (grammar->loop_id == *loop_id) {
	    if (ret_optional != NULL)
		*ret_optional = true;

	    return false;
	}

	grammar->loop_id = *loop_id;
    }

    switch (grammar->grammar_type) {
	case Grammar::t_Immediate: {
	    Grammar_Immediate_SingleToken * const grammar__immediate =
		    static_cast <Grammar_Immediate_SingleToken*> (grammar);

	    if (tranzition_entries != NULL) {
		if (grammar__immediate->getToken ().isNull () ||
		    grammar__immediate->getToken ()->getLength () == 0)
		{
		    if (!grammar__immediate->token_match_cb_name.isNull ()) {
			DEBUG_OPT (
			    errf->print ("  {").print (grammar__immediate->token_match_cb_name).print ("}");
			)
		    } else {
			abortIf (grammar__immediate->token_match_cb != NULL);
			DEBUG_OPT (
			    errf->print (" ANY");
			)

			param_switch_grammar_entry->any_tranzition = true;
		    }
		} else {
		    DEBUG_OPT (
			errf->print (" ").print (grammar__immediate->getToken ()).print ("");
		    )
		}

		if (!grammar__immediate->token_match_cb_name.isNull ()) {
		    Ref<TranzitionMatchEntry> tranzition_match_entry = grab (new TranzitionMatchEntry);
		    tranzition_match_entry->token_match_cb = grammar__immediate->token_match_cb;
		    DEBUG_OPT2 (
			errf->print ("--- TRANZITION MATCH ENTRY").pendl ();
		    )
		    tranzition_match_entries->append (tranzition_match_entry);
		} else
		if (!grammar__immediate->getToken ().isNull () &&
		    grammar__immediate->getToken ()->getLength () > 0)
		{
//		    Ref<TranzitionEntry> tranzition_entry = grab (new TranzitionEntry);
//		    tranzition_entry->token = grammar__immediate->getToken ();
//		    tranzitioin_entries.insert (tranzition_entry);

//		    tranzition_entries->insert (grammar__immediate->getToken ()->getData ());

		    SwitchGrammarEntry::TranzitionEntry * const tranzition_entry = new SwitchGrammarEntry::TranzitionEntry;
		    tranzition_entry->grammar_name = grab (new String (grammar__immediate->getToken()->getMemoryDesc()));

		    tranzition_entries->add (tranzition_entry);
		}
	    }

	    return true;
	} break;
	case Grammar::t_Compound: {
	    Grammar_Compound * const grammar__compound =
		    static_cast <Grammar_Compound*> (grammar);

	    Bool got_tranzition = false;
	    Bool optional = true;
	    List< Ref<CompoundGrammarEntry> >::DataIterator iter (grammar__compound->grammar_entries);
	    while (!iter.done ()) {
		Ref<CompoundGrammarEntry> &compound_grammar_entry = iter.next ();

		if (compound_grammar_entry->grammar.isNull ()) {
		  // _AcceptCb or _UniversalAcceptCb or UpwardsAnchor.
		    continue;
		}

		if (tranzition_entries != NULL) {
		    bool tmp_optional = false;
		    got_tranzition = do_optimizeGrammar (compound_grammar_entry->grammar,
							 tranzition_entries,
							 tranzition_match_entries,
							 param_switch_grammar_entry,
							 &tmp_optional,
							 loop_id);
		    if (!tmp_optional &&
			!(compound_grammar_entry->flags & CompoundGrammarEntry::Optional))
		    {
//			errf->print ("Z");
			optional = false;
		    }

		    if (!optional) {
//			errf->print ("X");
			break;
		    }
		} else {
		    do_optimizeGrammar (compound_grammar_entry->grammar,
					NULL /* tranzition_entries */,
					NULL /* tranzition_match_entries */,
					NULL /* switch_grammar_entry */,
					NULL /* ret_optional */,
					loop_id);
		}
	    }

	    if (ret_optional != NULL)
		*ret_optional = optional;

	    return got_tranzition;
	} break;
	case Grammar::t_Switch: {
	    Grammar_Switch * const grammar__switch =
		    static_cast <Grammar_Switch*> (grammar);

	    Bool got_tranzition = false;
	    Bool optional = true;
	    List< Ref<SwitchGrammarEntry> >::DataIterator iter (grammar__switch->grammar_entries);
	    while (!iter.done ()) {
		Ref<SwitchGrammarEntry> &switch_grammar_entry = iter.next ();

		if (tranzition_entries != NULL) {
		    bool tmp_optional = false;
		    got_tranzition = do_optimizeGrammar (switch_grammar_entry->grammar,
							 tranzition_entries,
							 tranzition_match_entries,
							 param_switch_grammar_entry,
							 &tmp_optional,
							 loop_id);
		    if (!tmp_optional &&
			!(switch_grammar_entry->flags & CompoundGrammarEntry::Optional))
		    {
			optional = false;
		    }
		} else {
		    DEBUG_OPT (
			errf->print ("").print (switch_grammar_entry->grammar->toString ()).print (": ");
		    )
		    {
			bool tmp_optional = false;
			do_optimizeGrammar (switch_grammar_entry->grammar,
					    &switch_grammar_entry->tranzition_entries,
					    &switch_grammar_entry->tranzition_match_entries,
					    switch_grammar_entry,
					    &tmp_optional,
					    loop_id);
			if (tmp_optional) {
			  // Fully optional grammars should not be upwards-optimized.
			  // We add 'any' token to force entering.
//			    switch_grammar_entry->tranzition_entries.append (grab (new TranzitionEntry));
			    switch_grammar_entry->any_tranzition = true;
			}
		    }
		    DEBUG_OPT (
			errf->print ("\n");
		    )

		    abortIf (*loop_id + 1 <= *loop_id);
		    (*loop_id) ++;
		    do_optimizeGrammar (switch_grammar_entry->grammar,
					NULL /* tranzition_entries */,
					NULL /* tranzition_match_entries */,
					NULL /* switch_grammar_entry */,
					NULL /* ret_optional */,
					loop_id);
		}
	    }

	    if (ret_optional != NULL)
		*ret_optional = optional;

	    return got_tranzition;
	} break;
	case Grammar::t_Alias: {
	    Grammar_Alias * const grammar_alias =
		    static_cast <Grammar_Alias*> (grammar);

	    return do_optimizeGrammar (grammar_alias->aliased_grammar,
				       tranzition_entries,
				       tranzition_match_entries,
				       param_switch_grammar_entry,
				       ret_optional,
				       loop_id);
	} break;
	default:
	    abortIfReached ();
    }

    // Unreachable
    abortIfReached ();
    return false;
}

void
optimizeGrammar (Grammar * const grammar /* non-null */)
{
    Size loop_id = 1;
    do_optimizeGrammar (grammar,
			NULL /* tranzition_entries */,
			NULL /* tranzition_match_entries */,
			NULL /* switch_grammar_entry */,
			NULL /* ret_optional */,
			&loop_id);
}

// Как работает парсер:
//
// Состояние парсера - список ступеней (ParsingStep). Текущая ступень находится в конце списка.
// Первая ступень - искуственно созданная, списочная (ParsingStep_Sequence), это список всех
// вхождений исходной грамматики.
//
// Условно полагаем, что корневая грамматика находится "внизу".
// Движение вверх (ParsingState::Up) - когда мы начинаем рассматривать новую грамматику.
// Движение вниз (ParsingState::Down) - когда мы рассмотрели грамматику и спускаемся на
// предыдущую ступень. Для перемещения на ступень вверх используем push_step(),
// вниз - pop_step().
//
// Ступени могут быть трёх типов: повторения (Sequence), последовательности (Compound) и
// вариативные (Switch).
//     Ступени Sequence описывают произвольное число вхождений подграмматики.
//     Ступени Compound задают строгую последовательность подграмматик.
//     Ступени Switch предполагают возможность вхождения одной из нескольких подграмматик.
//
void
parse (TokenStream    *token_stream,
       LookupData     *lookup_data,
       void           *user_data,
       Grammar        *grammar,
       ParserElement **ret_element,
       ConstMemoryDesc const &default_variant,
       ParserConfig   *parser_config,
       bool            debug_dump)
    throw (ParsingException,
	   IOException,
	   InternalException)
{
    abortIf (token_stream == NULL);
    abortIf (grammar == NULL);

    if (ret_element != NULL)
	*ret_element = NULL;

    Ref<ParserConfig> tmp_parser_config;
    if (parser_config == NULL) {
	tmp_parser_config = createDefaultParserConfig ();
	parser_config = tmp_parser_config;
    }

    Ref<ParsingState> parsing_state = grab (new ParsingState);
    parsing_state->parser_config = parser_config;
    parsing_state->nest_level = 0;
    parsing_state->token_stream = token_stream;
    parsing_state->lookup_data = lookup_data;
    parsing_state->user_data = user_data;
    parsing_state->cur_direction = ParsingState::Up;
    parsing_state->cur_positive_cache_entry = &parsing_state->positive_cache_root;
    parsing_state->negative_cache.goRight ();
    parsing_state->default_variant = default_variant;

    parsing_state->debug_dump = debug_dump;

#if 0
    {
	Ref<ParsingStep_Sequence> step = grab (new ParsingStep_Sequence);
	step->acceptor = grab (static_cast <Acceptor*> (new ListAcceptor<ParserElement> (out_list)));
	step->optional = true;
	step->grammar = grammar;
	token_stream->getPosition (&step->token_stream_pos);
	parsing_state->steps.append (step.ptr ());
    }
#endif

//    Ref< RefAcceptor<ParserElement> > acceptor = grab (new RefAcceptor<ParserElement> (ret_element));
    VSlabRef< PtrAcceptor<ParserElement> > acceptor =
	    VSlabRef< PtrAcceptor<ParserElement> >::forRef < PtrAcceptor<ParserElement> > (
		    parsing_state->ptr_acceptor_slab.alloc ());
    acceptor->init (ret_element);

    if (!parsing_state->lookup_data.isNull ())
	parsing_state->lookup_data->newCheckpoint ();

    ParsingResult pres = parse_grammar (parsing_state, grammar, acceptor, false /* optional */);
    if (pres == ParseNonemptyMatch ||
	pres == ParseEmptyMatch ||
	pres == ParseNoMatch)
    {
	return;
    }

    abortIf (pres != ParseUp);

    while (!parsing_state->step_list.isEmpty()) {
	switch (parsing_state->cur_direction) {
	    case ParsingState::Up:
		DEBUG_INT (
		  errf->print ("Pargen.parse: ParsingState::Up").pendl ();
		);
		parse_up (parsing_state);
		break;
	    case ParsingState::Down:
		DEBUG_INT (
		  errf->print ("Pargen.parse: ParsingState::Down").pendl ();
		);
		parse_down (parsing_state);
		break;
	    default:
		abortIfReached ();
	}
    }
}

}


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


#include <mycpp/util.h>
#include <mycpp/io.h>

#include <pargen/source_compiler.h>

#define DEBUG(a) a

using namespace MyCpp;

namespace Pargen {

static void
compileSource_Phrase (File *file,
		      Phrase const *phrase,
		      CompilationOptions const *opts,
		      ConstMemoryDesc const &declaration_name,
		      ConstMemoryDesc const &lowercase_declaration_name,
		      ConstMemoryDesc const &phrase_name,
		      bool subtype,
		      bool has_begin,
		      bool has_match,
		      bool has_accept)
    throw (CompilationException,
	   IOException,
	   InternalException)
{
    abortIf (file == NULL ||
	     phrase == NULL ||
	     opts == NULL);

    ConstMemoryDesc decl_name;
    ConstMemoryDesc lowercase_decl_name;
    bool global_grammar;
    if (compareByteArrays (declaration_name, ConstMemoryDesc ("*", countStrLength ("*"))) == ComparisonEqual) {
	decl_name = ConstMemoryDesc ("Grammar", countStrLength ("Grammar"));
	lowercase_decl_name = ConstMemoryDesc ("grammar", countStrLength ("grammar"));
	global_grammar = true;
    } else {
	decl_name = declaration_name;
	lowercase_decl_name = lowercase_declaration_name;
	global_grammar = false;
    }

    Ref<String> phrase_prefix;
    if (subtype)
	phrase_prefix = String::forPrintTask (Pt (decl_name) ((const char*) "_") (phrase_name));
    else
	phrase_prefix = String::forPrintTask (Pt (decl_name));

    {
	List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase->phrase_parts);
	while (!phrase_part_iter.done ()) {
	    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();

	    if (phrase_part->phrase_part_type == PhrasePart::_AcceptCb) {
		PhrasePart_AcceptCb *phrase_part__accept_cb = static_cast <PhrasePart_AcceptCb*> (phrase_part.ptr ());
		if (phrase_part__accept_cb->repetition)
		    continue;

		file->out ("bool ").out (phrase_part__accept_cb->cb_name).out (" (\n"
			   "        ").out (opts->capital_header_name).out ("_").out (decl_name).out (" *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n"
			   "bool __pargen_").out (phrase_part__accept_cb->cb_name).out (" (\n"
			   "        ParserElement *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data)\n"
			   "{\n"
			   "    ").out (opts->capital_header_name).out ("Element * const &_el =\n"
			   "            static_cast <").out (opts->capital_header_name).out ("Element*> (parser_element);\n"
			   "    abortIf (_el->").out (opts->header_name).out ("_element_type != "
				    ).out (opts->capital_header_name).out ("Element::_").out (decl_name).out (");\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("_").out (decl_name).out (" * const &el =\n"
			   "            static_cast <").out (opts->capital_header_name).out ("_").out (decl_name).out ("*> (_el);\n"
			   "\n"
			   "    return ").out (phrase_part__accept_cb->cb_name).out (" (el, parser_control, data);\n"
			   "}\n"
			   "\n");

		continue;
	    }

	    if (phrase_part->phrase_part_type == PhrasePart::_UniversalAcceptCb) {
		PhrasePart_UniversalAcceptCb *phrase_part__universal_accept_cb = static_cast <PhrasePart_UniversalAcceptCb*> (phrase_part.ptr ());
		if (phrase_part__universal_accept_cb->repetition)
		    continue;

		file->out ("bool ").out (phrase_part__universal_accept_cb->cb_name).out (" (\n"
			   "        ").out (opts->capital_header_name).out ("Element *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n"
			   "bool __pargen_").out (phrase_part__universal_accept_cb->cb_name).out (" (\n"
			   "        ParserElement *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data)\n"
			   "{\n"
			   "    ").out (opts->capital_header_name).out ("Element * const &_el =\n"
			   "            static_cast <").out (opts->capital_header_name).out ("Element*> (parser_element);\n").out (
			   "\n"
			   "    return ").out (phrase_part__universal_accept_cb->cb_name).out (" (_el, parser_control, data);\n"
			   "}\n"
			   "\n");

		continue;
	    }

	    if (phrase_part->phrase_part_type == PhrasePart::_UpwardsAnchor) {
		PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor =
			static_cast <PhrasePart_UpwardsAnchor*> (phrase_part.ptr ());

		if (!phrase_part__upwards_anchor->jump_cb_name.isNull ()) {
		    file->out ("bool ").out (phrase_part__upwards_anchor->jump_cb_name).out (" (\n"
			       "        ParserElement *parser_element,\n"
			       "        void *data);\n"
			       "\n");
		}

		continue;
	    }

	    if (phrase_part->phrase_part_type == PhrasePart::_Label) {
	      // TODO
		continue;
	    }

	    const char *name_to_set;
	    const char *type_to_set;
	    bool any_token = false;
	    if (phrase_part->phrase_part_type == PhrasePart::_Token) {
		PhrasePart_Token *phrase_part__token = static_cast <PhrasePart_Token*> (phrase_part.ptr ());
		if (phrase_part__token->token.isNull () ||
		    phrase_part__token->token->getLength () == 0)
		{
		    any_token = true;
		    name_to_set = "any_token";
		    // This doesn't matter for "any" token;
		    type_to_set = "__invalid_type__";
		} else {
		    continue;
		}
	    } else {
		abortIf (phrase_part->phrase_part_type != PhrasePart::_Phrase);
		
		PhrasePart_Phrase * const &phrase_part__phrase = static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

		name_to_set = phrase_part__phrase->name->getData ();
		type_to_set = phrase_part__phrase->decl_phrases->declaration_name->getData ();
	    }

	    file->out ("static void\n")
		       .out (opts->header_name).out ("_").out (phrase_prefix).out ("_set_").out (name_to_set).out (" (\n"
		       "        ParserElement *___self,\n"
		       "        ParserElement *___el)\n"
		       "{\n"
#if 0
		       "    abortIf (___self == NULL ||\n"
		       "             ___el == NULL);\n"
#endif
		       "    if (___self == NULL ||\n"
		       "        ___el == NULL)\n"
		       "    {\n"
		       "        return;\n"
		       "    }\n"
		       "\n"
		       "    ").out (opts->capital_header_name).out ("Element * const &__self = static_cast <").out (opts->capital_header_name).out ("Element*> (___self);\n");

	    if (!any_token) {
		file->out ("    ").out (opts->capital_header_name).out ("Element * const &__el = static_cast <").out (opts->capital_header_name).out ("Element*> (___el);\n");
	    }

	    file->out ("\n"
		       "    abortIf (__self->").out (opts->header_name).out ("_element_type != ").out (opts->capital_header_name).out ("Element::_").out (decl_name);
		       
	    if (!any_token) {
		file->out (" ||\n"
			   "             __el->").out (opts->header_name).out ("_element_type != ").out (opts->capital_header_name).out ("Element::_").out (type_to_set);
	    }
	    
	    file->out (");\n"
		       "\n");

	    if (subtype) {
		file->out ("    ").out (opts->capital_header_name).out ("_").out (decl_name).out (" * const &_self = static_cast <").out (opts->capital_header_name).out ("_").out (decl_name).out ("*> (__self);\n"
			   "\n"
			   "    abortIf (_self->").out (lowercase_decl_name).out ("_type").out (" != ").out (opts->capital_header_name).out ("_").out (decl_name).out ("::_").out (phrase_name).out (");\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("_").out (decl_name).out ("_").out (phrase_name).out (" * const &self = static_cast <").out (opts->capital_header_name).out ("_").out (decl_name).out ("_").out (phrase_name).out ("*> (_self);\n"
			   "\n");
	    } else {
		file->out  ("    ").out (opts->capital_header_name).out ("_").out (decl_name).out (" * const &self = static_cast <").out (opts->capital_header_name).out ("_").out (decl_name).out ("*> (__self);\n"
			    "\n");
	    }

	    file->out ("    self->").out (name_to_set);

	    if (any_token) {
		file->out (" = static_cast <ParserElement_Token*> (___el);\n");
	    } else {
		if (phrase_part->seq)
		    file->out ("s.append (static_cast <").out (opts->capital_header_name).out ("_").out (type_to_set).out ("*> (__el));\n");
		else
		    file->out (" = static_cast <").out (opts->capital_header_name).out ("_").out (type_to_set).out ("*> (___el);\n");
	    }

	    file->out ("}\n"
		       "\n");
	}
    }

    file->out ("static ParserElement*\n")
	       .out (opts->header_name).out ("_").out (phrase_prefix).out ("_creation_func (VStack * const vstack /* non-null */)\n"
	       "{\n"
	       "    return new (vstack->push_malign (sizeof (").out (opts->capital_header_name).out ("_").out (phrase_prefix).out ("))) ").out (opts->capital_header_name).out ("_").out (phrase_prefix).out (";\n"
	       "}\n"
	       "\n");
//	       "    return grab (static_cast <ParserElement*> (new ").out (opts->capital_header_name).out ("_").out (phrase_prefix).out ("));\n"

    if (!global_grammar)
	file->out ("static ");
    file->out ("Ref<Grammar>\n"
	       "create_").out (opts->header_name).out ("_").out (global_grammar ? "grammar" : phrase_prefix->getData ()).out (" ()\n"
	       "{\n"
	       "    static Ref<Grammar_Compound> grammar;\n"
	       "    if (!grammar.isNull ())\n"
	       "        return grammar.ptr ();\n"
	       "\n"
	       "    grammar = grab (new Grammar_Compound (").out (opts->header_name).out ("_").out (phrase_prefix).out ("_creation_func));\n"
	       "    grammar->name = String::forData (\"").out (phrase_prefix).out ("\");\n");

    if (has_begin)
	file->out ("    grammar->begin_func = ").out (opts->header_name).out ("_").out (phrase_prefix).out ("_begin_func;\n");
    if (has_match)
	file->out ("    grammar->match_func = __pargen_").out (opts->header_name).out ("_").out (phrase_prefix).out ("_match_func;\n");
    if (has_accept)
	file->out ("    grammar->accept_func = __pargen_").out (opts->header_name).out ("_").out (phrase_prefix).out ("_accept_func;\n");

    file->out ("\n");

    {
	List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase->phrase_parts);
	while (!phrase_part_iter.done ()) {
	    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();

	    if (phrase_part->phrase_part_type == PhrasePart::_Label)
		continue;

	    file->out ("    {\n"
		       "        Ref<CompoundGrammarEntry> entry = grab (new CompoundGrammarEntry ());\n");

	    if (phrase_part->opt)
		file->out ("        entry->flags |= CompoundGrammarEntry::Optional;\n");

	    if (phrase_part->seq)
		file->out ("        entry->flags |= CompoundGrammarEntry::Sequence;\n");

	    switch (phrase_part->phrase_part_type) {
		case PhrasePart::_Phrase: {
		    PhrasePart_Phrase * const &phrase_part__phrase =
			    static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

		    file->out ("        entry->grammar = "
						"create_").out (opts->header_name).out (
						"_").out (phrase_part__phrase->phrase_name).out (" ();\n"
			       "        entry->assignment_func = "
						).out (opts->header_name).out ("_").out (phrase_prefix).out (
						"_set_").out (phrase_part__phrase->name).out (";\n");
		} break;
		case PhrasePart::_Token: {
		    PhrasePart_Token * const &phrase_part__token =
			    static_cast <PhrasePart_Token*> (phrase_part.ptr ());

		    file->out ("\n"
			       "        Ref<Grammar_Immediate_SingleToken> grammar__immediate = "
						"grab (new Grammar_Immediate_SingleToken ("
							"\"").out (phrase_part__token->token).out ("\"));\n");
		    if (!phrase_part__token->token_match_cb.isNull () &&
			phrase_part__token->token_match_cb->getLength () > 0)
		    {
			file->out ("\n"
				   "        bool ").out (phrase_part__token->token_match_cb).out (" (\n"
				   "                ConstMemoryDesc const &token,\n"
				   "                void                  *token_user_ptr,\n"
				   "                void                  *user_data);\n"
				   "\n");

			file->out ("        grammar__immediate->token_match_cb = "
						    ).out (phrase_part__token->token_match_cb).out (";\n");
			file->out ("        grammar__immediate->token_match_cb_name = "
						    "grab (new String ("
							    "\"").out (phrase_part__token->token_match_cb).out ("\"));\n"
				   "\n");
		    }
		    file->out ("        entry->grammar = grammar__immediate;\n");

		    if (phrase_part__token->token.isNull () ||
			phrase_part__token->token->getLength () == 0)
		    {
			file->out ("        entry->assignment_func = "
						    ).out (opts->header_name).out ("_").out (phrase_prefix).out (
						    "_set_").out ("any_token").out (";\n");
		    }
		} break;
		case PhrasePart::_AcceptCb: {
		    PhrasePart_AcceptCb * const &phrase_part__match_cb =
			    static_cast <PhrasePart_AcceptCb*> (phrase_part.ptr ());

		    file->out ("        entry->inline_match_func = __pargen_").out (phrase_part__match_cb->cb_name).out (";\n");
		} break;
		case PhrasePart::_UniversalAcceptCb: {
		    PhrasePart_UniversalAcceptCb * const &phrase_part__match_cb =
			    static_cast <PhrasePart_UniversalAcceptCb*> (phrase_part.ptr ());

		    file->out ("        entry->inline_match_func = __pargen_").out (phrase_part__match_cb->cb_name).out (";\n");
		} break;
		case PhrasePart::_UpwardsAnchor: {
		    PhrasePart_UpwardsAnchor * const phrase_part__upwards_anchor =
			    static_cast <PhrasePart_UpwardsAnchor*> (phrase_part.ptr ());

		    file->out ("        entry->is_jump = true;\n"
			       "        entry->jump_grammar = create_").out (opts->header_name).out ("_"
						).out (phrase_part__upwards_anchor->declaration_name).out (" ();\n");
		    if (!phrase_part__upwards_anchor->jump_cb_name.isNull ()) {
			file->out ("        entry->jump_cb = ").out (phrase_part__upwards_anchor->jump_cb_name).out (";\n");
		    }
		    file->out ("        entry->jump_switch_grammar_index = "
						).out (phrase_part__upwards_anchor->switch_grammar_index).out (";\n"
			       "        entry->jump_compound_grammar_index = "
						).out (phrase_part__upwards_anchor->compound_grammar_index).out (";\n");
		} break;
		case PhrasePart::_Label: {
		    abortIfReached ();
		} break;
		default:
		    abortIfReached ();
	    }

	    file->out ("        grammar->grammar_entries.append (entry);\n"
		       "    }\n"
		       "\n");
	}
    }

    file->out ("    return grammar.ptr ();\n"
	       "}\n"
	       "\n");

  // Dumping

//    if (!global_grammar)
//	file->out ("static ");
    file->out ("void\n"
	       "dump_").out (opts->header_name).out ("_")
		       .out (global_grammar ? "grammar" : phrase_prefix->getData ())
		       .out (" (").out (opts->capital_header_name).out ("_").out (phrase_prefix)
		       .out(" const *el").out (global_grammar ? "" : ", Size nest_level").out (")\n"
	       "{\n"
	       "    if (el == NULL) {\n");

    if (!global_grammar)
	file->out ("        print_tab (errf, nest_level);\n");
    else
	file->out ("        print_tab (errf, 0);\n");

    file->out ("        errf->print (\"(null) ").out (phrase_prefix).out ("\\n\");\n"
	       "        return;\n"
	       "    }\n"
	       "\n");

    if (!global_grammar)
	file->out ("    print_tab (errf, nest_level);\n");

    file->out ("    errf->print (\"").out (phrase_prefix).out ("\\n\");\n"
	       "\n");

    {
	List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase->phrase_parts);
	while (!phrase_part_iter.done ()) {
	    Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();
	    switch (phrase_part->phrase_part_type) {
		case PhrasePart::_Phrase: {
		    PhrasePart_Phrase * const &phrase_part__phrase = static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

		    if (phrase_part->seq) {
			file->out ("    {\n"
				   "        List<").out (opts->capital_header_name).out ("_")
						  .out (phrase_part__phrase->decl_phrases->declaration_name)
						  .out ("*>::DataIterator sub_iter (el->")
						  .out (phrase_part__phrase->name).out ("s);\n"
				   "        while (!sub_iter.done ()) {\n"
				   "            ").out (opts->capital_header_name).out ("_")
							.out (phrase_part__phrase->decl_phrases->declaration_name)
							.out (" *&sub = sub_iter.next ();\n"
				   "            dump_").out (opts->header_name).out ("_")
							.out (phrase_part__phrase->decl_phrases->declaration_name)
							.out (" (sub, ").out (global_grammar ? "1" : "nest_level + 1").out (");\n"
				   "        }\n"
				   "    }\n"
				   "\n");
		    } else {
			file->out ("    dump_").out (opts->header_name).out ("_")
						.out (phrase_part__phrase->decl_phrases->declaration_name)
						.out (" (el->").out (phrase_part__phrase->name).out (", ")
						.out (global_grammar ? "1" : "nest_level + 1").out (");\n"
				   "\n");
		    }
		} break;
		case PhrasePart::_Token: {
		    PhrasePart_Token * const &phrase_part__token = static_cast <PhrasePart_Token*> (phrase_part.ptr ());

		    if (!global_grammar)
			file->out ("    print_tab (errf, nest_level + 1);\n");

		    file->out ("    errf->print (\"token: ").out (phrase_part__token->token);
		    if (phrase_part->opt || phrase_part->seq) {
			if (phrase_part->opt)
			    file->out ("_opt");
			if (phrase_part->seq)
			    file->out ("_seq");
		    }
		    file->out ("\\n\");\n");
		} break;
		case PhrasePart::_AcceptCb: {
		  // No-op
		} break;
		case PhrasePart::_UniversalAcceptCb: {
		  // No-op
		} break;
		case PhrasePart::_UpwardsAnchor: {
		  // No-op
		} break;
		case PhrasePart::_Label: {
		  // No-op
		} break;
		default:
		    abortIfReached ();
	    }
	}
    }

    file->out ("}\n"
	       "\n");
}

static void
compileSource_Alias (File *file,
		     Declaration_Phrases *decl_phrases,
		     CompilationOptions const *opts,
		     bool has_begin,
		     bool has_match,
		     bool has_accept)
{
    Declaration const * const decl = decl_phrases;

    abortIf (!decl_phrases->is_alias);

    const char *decl_name;
    const char *lowercase_decl_name;
    bool global_grammar;
    abortIf (decl->declaration_name.isNull ());
    if (compareStrings (decl->declaration_name->getData (), "*")) {
	decl_name = "Grammar";
	lowercase_decl_name = "grammar";
	global_grammar = true;
    } else {
	decl_name = decl->declaration_name->getData ();
	lowercase_decl_name = decl->lowercase_declaration_name->getData ();
	global_grammar = false;
    }

    // TODO
    Ref<String> phrase_prefix = decl->declaration_name;

    if (!global_grammar)
	file->out ("static ");
    file->out ("Ref<Grammar>\n"
	       "create_").out (opts->header_name).out ("_").out (global_grammar ? "grammar" : phrase_prefix->getData ()).out (" ()\n"
	       "{\n"
	       "    static Ref<Grammar_Alias> grammar;\n"
	       "    if (!grammar.isNull ())\n"
	       "        return grammar.ptr ();\n"
	       "\n"
	       "    grammar = grab (new Grammar_Alias);\n"
	       "    grammar->name = String::forData (\"").out (phrase_prefix).out ("\");\n"
	       "    grammar->aliased_grammar = create_").out (opts->header_name).out ("_").out (decl_phrases->aliased_name).out (" ();\n"
	       "\n");

    if (has_begin)
	file->out ("    grammar->begin_func = ").out (opts->header_name).out ("_").out (phrase_prefix).out ("_begin_func;\n");
    if (has_match)
	file->out ("    grammar->match_func = __pargen_").out (opts->header_name).out ("_").out (phrase_prefix).out ("_match_func;\n");
    if (has_accept)
	file->out ("    grammar->accept_func = __pargen_").out (opts->header_name).out ("_").out (phrase_prefix).out ("_accept_func;\n");

    file->out ("\n");

    file->out ("    return grammar.ptr ();\n"
	       "}\n"
	       "\n");

#if 0
  // Dumping

    file->out ("void\n"
	       "dump_").out (opts->header_name).out ("_").out (global_grammar ? "grammar" : phrase_prefix->getData ()).out (" (").out (opts->capital_header_name).out ("_").out (phrase_prefix).out(" const *el").out (global_grammar ? "" : ", Size nest_level").out (")\n"
	       "{\n"
	       "    if (el == NULL) {\n");

    if (!global_grammar)
	file->out ("        print_tab (errf, nest_level);\n");
    else
	file->out ("        print_tab (errf, 0);\n");

    file->out ("        errf->print (\"(null) ").out (phrase_prefix).out ("\\n\");\n"
	       "        return;\n"
	       "    }\n"
	       "\n");

    if (!global_grammar)
	file->out ("    print_tab (errf, nest_level);\n");

    file->out ("    errf->print (\"").out (phrase_prefix).out ("\\n\");\n"
	       "\n");

    file->out ("}\n"
	       "\n");
#endif
}

void
compileSource (File *file,
	       PargenTask const * pargen_task,
	       CompilationOptions const *opts)
    throw (CompilationException,
	   IOException,
	   InternalException)
{
    abortIf (file == NULL ||
	     pargen_task == NULL ||
	     opts == NULL);

    file->out ("#include <mycpp/util.h>\n"
	       "#include <mycpp/io.h>\n"
	       "\n"
	       "#include \"").out (opts->header_name).out ("_pargen.h\"\n"
	       "\n"
	       "using namespace MyCpp;\n"
	       "using namespace Pargen;\n"
	       "\n"
	       "namespace ").out (opts->capital_module_name).out (" {\n"
	       "\n"
	       "static void\n"
	       "print_whsp (File *file,\n"
	       "            Size num_spaces)\n"
	       "{\n"
	       "    for (Size i = 0; i < num_spaces; i++)\n"
	       "        file->print (\" \");\n"
	       "}\n"
	       "\n"
	       "static void\n"
	       "print_tab (File *file,\n"
	       "           Size nest_level)\n"
	       "{\n"
	       "    print_whsp (file, nest_level * 1);\n"
	       "}\n"
	       "\n");

    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	while (!decl_iter.done ()) {
	    Ref<Declaration> &decl = decl_iter.next ();
	    if (decl->declaration_type != Declaration::_Phrases)
		continue;

	    if (compareStrings (decl->declaration_name->getData (), "*"))
		continue;

	    file->out ("static Ref<Grammar> create_").out (opts->header_name).out ("_").out (decl->declaration_name).out (" ();\n");
//	    file->out ("static void dump_").out (opts->header_name).out ("_").out (decl->declaration_name).out (" (").out (opts->capital_header_name).out ("_").out (decl->declaration_name).out (" const *, Size);\n");
	}
	file->out ("\n");
    }

    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	while (!decl_iter.done ()) {
	    Ref<Declaration> &decl = decl_iter.next ();

	    if (decl->declaration_type != Declaration::_Phrases)
		continue;

	    Declaration_Phrases * const decl_phrases = static_cast <Declaration_Phrases*> (decl.ptr ());

	    const char *decl_name;
	    const char *lowercase_decl_name;
	    bool global_grammar;
	    abortIf (decl->declaration_name.isNull ());
	    if (compareStrings (decl->declaration_name->getData (), "*")) {
		decl_name = "Grammar";
		lowercase_decl_name = "grammar";
		global_grammar = true;
	    } else {
		decl_name = decl->declaration_name->getData ();
		lowercase_decl_name = decl->lowercase_declaration_name->getData ();
		global_grammar = false;
	    }

	    const char *elem_name = decl_name;
	    if (decl_phrases->is_alias)
		elem_name = decl_phrases->deep_aliased_name->getData ();

	    bool has_begin = false;
	    if (!decl_phrases->callbacks.lookup ("begin").isNull ()) {
		has_begin = true;
		file->out ("void ").out (opts->header_name).out ("_").out (decl_name).out ("_begin_func (\n"
			   "        void *data);\n"
			   "\n");
	    }

	    bool has_match = false;
	    if (!decl_phrases->callbacks.lookup ("match").isNull ()) {
		has_match = true;
		file->out ("bool ").out (opts->header_name).out ("_").out (decl_name).out ("_match_func (\n"
			   "        ").out (opts->capital_header_name).out ("_").out (elem_name).out (" *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n"
			   "bool __pargen_").out (opts->header_name).out ("_").out (decl_name).out ("_match_func (\n"
			   "        ParserElement *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data)\n"
			   "{\n"
			   "    ").out (opts->capital_header_name).out ("Element * const &_el = static_cast <").out (opts->capital_header_name).out ("Element*> (parser_element);\n"
			   "    abortIf (_el->").out (opts->header_name).out ("_element_type != ").out (opts->capital_header_name).out ("Element::_").out (elem_name).out (");\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("_").out (elem_name).out (" * const &el = static_cast <").out (opts->capital_header_name).out ("_").out (elem_name).out ("*> (_el);\n"
			   "\n"
			   "    return ").out (opts->header_name).out ("_").out (decl_name).out ("_match_func (el, parser_control, data);\n"
			   "}\n"
			   "\n");
	    }

	    bool has_accept = false;
	    if (!decl_phrases->callbacks.lookup ("accept").isNull ()) {
		has_accept = true;
		file->out ("void ").out (opts->header_name).out ("_").out (decl_name).out ("_accept_func (\n"
			   "        ").out (opts->capital_header_name).out ("_").out (elem_name).out (" *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n"
			   "void __pargen_").out (opts->header_name).out ("_").out (decl_name).out ("_accept_func (\n"
			   "        ParserElement *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data)\n"
			   "{\n"
			   "    if (parser_element == NULL) {\n"
			   "        ").out (opts->header_name).out ("_").out (decl_name).out ("_accept_func (NULL, parser_control, data);\n"
			   "        return;\n"
			   "    }\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("Element * const &_el = static_cast <").out (opts->capital_header_name).out ("Element*> (parser_element);\n"
			   "    abortIf (_el->").out (opts->header_name).out ("_element_type != ").out (opts->capital_header_name).out ("Element::_").out (elem_name).out (");\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("_").out (elem_name).out (" * const &el = static_cast <").out (opts->capital_header_name).out ("_").out (elem_name).out ("*> (_el);\n"
			   "\n"
			   "    ").out (opts->header_name).out ("_").out (decl_name).out ("_accept_func (el, parser_control, data);\n"
			   "}\n"
			   "\n");
	    }

	    if (decl_phrases->phrases.getNumElements () > 1) {
		{
		    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
		    while (!phrase_iter.done ()) {
			Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
			abortIf (phrase_record.isNull () ||
				 phrase_record->phrase.isNull () ||
				 phrase_record->phrase->phrase_name.isNull ());
			Phrase *phrase = phrase_record->phrase;
			compileSource_Phrase (file,
					      phrase,
					      opts,
					      ConstMemoryDesc (decl_name, countStrLength (decl_name)),
					      ConstMemoryDesc (lowercase_decl_name, countStrLength (lowercase_decl_name)),
					      phrase->phrase_name->getMemoryDesc (),
					      true /* subtype */,
					      false /* has_begin */,
					      false /* has_match */,
					      false /* has_accept */);
		    }
		}

		if (!compareStrings (decl->declaration_name->getData (), "*"))
		    file->out ("static ");
		file->out ("Ref<Grammar>\n"
			   "create_").out (opts->header_name).out ("_").out (global_grammar ? "grammar" : decl_name).out (" ()\n"
			   "{\n"
			   "    static Ref<Grammar_Switch> grammar;\n"
			   "    if (!grammar.isNull ())\n"
			   "        return grammar.ptr ();\n"
			   "\n"
			   "    grammar = grab (new Grammar_Switch ());\n"
			   "    grammar->name = String::forData (\"").out (decl_name).out ("\");\n");

		if (has_begin)
		    file->out ("    grammar->begin_func = ").out (opts->header_name).out ("_").out (decl_name).out ("_begin_func;\n");
		if (has_match)
		    file->out ("    grammar->match_func = __pargen_").out (opts->header_name).out ("_").out (decl_name).out ("_match_func;\n");
		if (has_accept)
		    file->out ("    grammar->accept_func = __pargen_").out (opts->header_name).out ("_").out (decl_name).out ("_accept_func;\n");

		file->out ("\n");

		{
		    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
		    while (!phrase_iter.done ()) {
			Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
			Phrase *phrase = phrase_record->phrase;
			file->out ("    {\n"
				   "        Ref<SwitchGrammarEntry> entry = grab (new SwitchGrammarEntry ());\n"
				   "        entry->grammar = create_").out (opts->header_name).out ("_").out (decl_name).out ("_").out (phrase->phrase_name).out (" ();\n");

			List< Ref<String> >::DataIterator variant_iter (phrase_record->variant_names);
			while (!variant_iter.done ()) {
			    Ref<String> &variant = variant_iter.next ();
			    file->out ("    entry->variants.append (String::forData (\"").out (variant).out ("\"));\n");
			}

			file->out ("\n"
				   "        grammar->grammar_entries.append (entry);\n"
				   "    }\n"
				   "\n");
		    }
		}

		file->out ("    return grammar.ptr ();\n"
			   "}\n"
			   "\n");

	      // Dumping

//		if (!global_grammar)
//		    file->out ("static ");
		file->out ("void\n"
			   "dump_").out (opts->header_name).out ("_").out (global_grammar ? "grammar" : decl_name).out (" (").out (opts->capital_header_name).out ("_").out (decl_name).out(" const *el").out (global_grammar ? "" : ", Size nest_level").out (")\n"
			   "{\n"
			   "    if (el == NULL) {\n");

		if (!global_grammar)
		    file->out ("        print_tab (errf, nest_level);\n");
		else
		    file->out ("        print_tab (errf, 0);\n");

		file->out ("        errf->print (\"(null) ").out (decl_name).out ("\\n\");\n"
			   "        return;\n"
			   "    }\n"
			   "\n"
			   "    switch (el->").out (lowercase_decl_name).out ("_type) {\n");

		{
		    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
		    while (!phrase_iter.done ()) {
			Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
			Phrase *phrase = phrase_record->phrase;

			file->out ("        case ").out (opts->capital_header_name).out ("_").out (decl_name).out ("::_").out (phrase->phrase_name).out (":\n"
				   "            dump_").out (opts->header_name).out ("_").out (decl_name).out ("_").out (phrase->phrase_name).out (" (static_cast <").out (opts->capital_header_name).out ("_").out (decl_name).out ("_").out (phrase->phrase_name).out (" const *> (el), ").out (global_grammar ? "0" : "nest_level + 0").out (");\n"
				   "            break;\n");
		    }
		}

		file->out ("        default:\n"
			   "            abortIfReached ();\n"
			   "    }\n"
			   "}\n"
			   "\n");
	    } else {
	      // The grammar consists of only one phrase.
	      // Aliases go this way.

		if (decl_phrases->is_alias) {
		    compileSource_Alias (file, decl_phrases, opts, has_begin, has_match, has_accept);
		} else {
		    if (!decl_phrases->phrases.isEmpty ()) {
			Ref<Phrase> &phrase = decl_phrases->phrases.first->data->phrase;
			abortIf (phrase.isNull ());
			compileSource_Phrase (file,
					      phrase,
					      opts,
					      decl->declaration_name->getMemoryDesc (),
					      decl->lowercase_declaration_name->getMemoryDesc (),
					      ConstMemoryDesc (),
					      false /* subtype */,
					      has_begin,
					      has_match,
					      has_accept);
		    }
		}
	    }
	}
    }

    file->out ("}\n"
	       "\n");
}

}


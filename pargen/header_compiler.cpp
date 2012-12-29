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

#include <pargen/header_compiler.h>


#define DEBUG(a) ;

#define FUNC_NAME(a) :


using namespace MyCpp;

namespace Pargen {

static void
compileHeader_Phrase_External (File                                    * const file,
			       Declaration_Phrases::PhraseRecord const * const phrase_record,
			       CompilationOptions                const * const opts,
			       ConstMemoryDesc                   const &declaration_name)
{
    abortIf (file == NULL ||
	     phrase_record == NULL ||
	     opts == NULL);

    ConstMemoryDesc decl_name;
    if (compareByteArrays (declaration_name, ConstMemoryDesc ("*", countStrLength ("*"))) == ComparisonEqual) {
	decl_name = ConstMemoryDesc ("Grammar", countStrLength ("Grammar"));
    } else {
	decl_name = declaration_name;
    }

    List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase_record->phrase->phrase_parts);
    while (!phrase_part_iter.done ()) {
	Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();

	switch (phrase_part->phrase_part_type) {
	    case PhrasePart::t_Phrase: {
	      // No-op
	    } break;
	    case PhrasePart::t_Token: {
	      // No-op
	    } break;
	    case PhrasePart::t_AcceptCb: {
		PhrasePart_AcceptCb *phrase_part__accept_cb =
			static_cast <PhrasePart_AcceptCb*> (phrase_part.ptr ());
		if (phrase_part__accept_cb->repetition)
		    continue;

		file->out ("bool ").out (phrase_part__accept_cb->cb_name).out (" (\n"
			   "        ").out (opts->capital_header_name).out ("_").out (decl_name).out (" *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n");
	    } break;
	    case PhrasePart::t_UniversalAcceptCb: {
		PhrasePart_UniversalAcceptCb *phrase_part__universal_accept_cb =
			static_cast <PhrasePart_UniversalAcceptCb*> (phrase_part.ptr ());
		if (phrase_part__universal_accept_cb->repetition)
		    continue;

		file->out ("bool ").out (phrase_part__universal_accept_cb->cb_name).out (" (\n"
			   "        ").out (opts->capital_header_name).out ("Element *parser_element,\n"
			   "        Pargen::ParserControl *parser_control,\n"
			   "        void *data);\n"
			   "\n");
	    } break;
	    case PhrasePart::t_UpwardsAnchor: {
	      // No-op
	    } break;
	    case PhrasePart::t_Label: {
	      // No-op
	    } break;
	    default:
		abortIfReached ();
	}
    }
}

static void
compileHeader_Phrase (File                                    * const file,
		      Declaration_Phrases::PhraseRecord const * const phrase_record,
		      CompilationOptions                const * const opts,
		      ConstMemoryDesc                   const & /* declaration_name */,
		      bool                                      const initializers_mode = false)
    throw (CompilationException,
	   IOException,
	   InternalException)
{
    abortIf (file == NULL ||
	     phrase_record == NULL ||
	     opts == NULL);

    List< Ref<PhrasePart> >::DataIterator phrase_part_iter (phrase_record->phrase->phrase_parts);
    while (!phrase_part_iter.done ()) {
	Ref<PhrasePart> &phrase_part = phrase_part_iter.next ();

	switch (phrase_part->phrase_part_type) {
	    case PhrasePart::t_Phrase: {
		PhrasePart_Phrase *phrase_part__phrase = static_cast <PhrasePart_Phrase*> (phrase_part.ptr ());

		if (phrase_part__phrase->seq) {
		    if (!initializers_mode) {
			file->out ("    MyCpp::List<").out (opts->capital_header_name).out ("_"
					     ).out (phrase_part__phrase->decl_phrases->declaration_name).out ("*> "
					     ).out (phrase_part__phrase->name).out ("s;\n");
		    }
		} else {
		    if (initializers_mode) {
			file->out ("          , ").out (phrase_part__phrase->name).out (" (NULL)\n");
		    } else {
			file->out ("    ").out (opts->capital_header_name).out ("_"
					     ).out (phrase_part__phrase->decl_phrases->declaration_name).out (" *"
					     ).out (phrase_part__phrase->name).out (";\n");
		    }
		}
	    } break;
	    case PhrasePart::t_Token: {
		PhrasePart_Token *phrase_part__token = static_cast <PhrasePart_Token*> (phrase_part.ptr ());

		if (phrase_part__token->token.isNull () ||
		    phrase_part__token->token->getLength () == 0)
		{
		    if (initializers_mode) {
			file->out ("          , any_token (NULL)\n");
		    } else {
			file->out ("    Pargen::ParserElement_Token *any_token;\n");
		    }
		}
	    } break;
	    case PhrasePart::t_AcceptCb: {
	      // No-op
	    } break;
	    case PhrasePart::t_UniversalAcceptCb: {
	      // No-op
	    } break;
	    case PhrasePart::t_UpwardsAnchor: {
	      // No-op
	    } break;
	    case PhrasePart::t_Label: {
	      // No-op
	    } break;
	    default:
		abortIfReached ();
	}
    }
}

void
compileHeader (File *file,
	       PargenTask const *pargen_task,
	       CompilationOptions const *opts)
    throw (CompilationException,
	   IOException,
	   InternalException)
{
    abortIf (file == NULL ||
	     pargen_task == NULL ||
	     opts == NULL);

    file->out ("#ifndef __").out (opts->all_caps_module_name).out ("__").out (opts->all_caps_header_name).out ("_PARGEN_H__\n"
	       "#define __").out (opts->all_caps_module_name).out ("__").out (opts->all_caps_header_name).out ("_PARGEN_H__\n"
	       "\n"
	       "#include <mycpp/string.h>\n"
	       "\n"
	       "#include <pargen/parser_element.h>\n"
	       "#include <pargen/grammar.h>\n"
	       "\n"
	       "namespace ").out (opts->capital_namespace_name).out (" {\n"
	       "\n"
	       "class ").out (opts->capital_header_name).out ("Element : public Pargen::ParserElement\n"
	       "{\n"
	       "public:\n"
	       "    enum Type {");

#if 0
// This collides MyCpp and LibMary.
	       "using namespace MyCpp;\n"
	       "\n"
#endif

    Bool got_global_grammar;
    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	Bool decl_iter_started;
	while (!decl_iter.done ()) {
	    Ref<Declaration> &_decl = decl_iter.next ();
	    if (_decl->declaration_type != Declaration::t_Phrases)
		continue;

	    Declaration_Phrases const * const decl = static_cast <Declaration_Phrases const *> (_decl.ptr ());

	    if (decl->is_alias)
		continue;

	    if (decl_iter_started)
		file->out (",\n");
	    else {
		decl_iter_started = true;
		file->out ("\n");
	    }

	    if (compareStrings (decl->declaration_name->getData (), "*")) {
		got_global_grammar = true;
		file->out ("        t_Grammar");
	    } else
		file->out ("        t_").out (decl->declaration_name);
	}
    }

    file->out ("\n"
	       "    };\n"
	       "\n"
	       "    const Type ").out (opts->header_name).out ("_element_type;\n"
	       "\n"
	       "    ").out (opts->capital_header_name).out ("Element (Type type)\n"
	       "        : ").out (opts->header_name).out ("_element_type (type)\n"
	       "    {\n"
	       "    }\n"
	       "};\n"
	       "\n");

    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	while (!decl_iter.done ()) {
	    Ref<Declaration> &_decl = decl_iter.next ();
	    if (_decl->declaration_type != Declaration::t_Phrases)
		continue;

	    Declaration_Phrases const * const decl = static_cast <Declaration_Phrases const *> (_decl.ptr ()); 

	    if (decl->is_alias)
		continue;

	    if (compareStrings (decl->declaration_name->getData (), "*"))
		continue;

	    file->out ("class ").out (opts->capital_header_name).out ("_").out (decl->declaration_name).out (";\n");
	}
	file->out ("\n");
    }

    {
	List< Ref<Declaration> >::DataIterator decl_iter (pargen_task->decls);
	while (!decl_iter.done ()) {
	    Ref<Declaration> &decl = decl_iter.next ();
	    if (decl->declaration_type != Declaration::t_Phrases)
		continue;

	    Declaration_Phrases const * const &decl_phrases = static_cast <Declaration_Phrases const *> (decl.ptr ());

	    if (decl_phrases->is_alias)
		continue;

	    const char *decl_name;
	    const char *lowercase_decl_name;
	    if (compareStrings (decl->declaration_name->getData (), "*")) {
		decl_name = "Grammar";
		lowercase_decl_name = "grammar";
	    } else {
		decl_name = decl->declaration_name->getData ();
		lowercase_decl_name = decl->lowercase_declaration_name->getData ();
	    }

	    compileHeader_Phrase_External (file, decl_phrases->phrases.first->data, opts, ConstMemoryDesc (decl_name, countStrLength (decl_name)));

	    file->out ("class ").out (opts->capital_header_name).out ("_").out (decl_name).out (" : "
			       "public ").out (opts->capital_header_name).out ("Element\n"
		       "{\n"
		       "public:\n");

	    DEBUG (
		errf->print ("Pargen.compileHeader: "
			     "decl->declaration_name: ").print (decl->declaration_name).print (", "
			     "decl_phrases->phrases.getNumElements(): ").print (decl_phrases->phrases.getNumElements ()).pendl ();
	    )
	    if (decl_phrases->phrases.getNumElements () > 1) {
		file->out ("    enum Type {");

		{
		    Size phrase_index = 0;
		    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
		    Bool phrase_iter_started;
		    while (!phrase_iter.done ()) {
			Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
			Phrase *phrase = phrase_record->phrase;

			if (phrase_iter_started)
			    file->out (",\n");
			else {
			    phrase_iter_started = true;
			    file->out ("\n");
			}

			Ref<String> phrase_name;
			if (phrase->phrase_name.isNull ())
			    phrase_name = String::forPrintTask (Pt ("phrase") (phrase_index));
			else
			    phrase_name = phrase->phrase_name;

			file->out ("        t_").out (phrase_name);
		    }
		}

		file->out ("\n"
			   "    };\n"
			   "\n"
			   "    const Type ").out (lowercase_decl_name).out ("_type;\n"
			   "\n"
			   "    ").out (opts->capital_header_name).out ("_").out (decl_name).out (" (Type type)\n"
			   "        : ").out (opts->capital_header_name).out ("Element ("
					       ).out (opts->capital_header_name).out ("Element::t_").out (decl_name).out ("),\n"
			   "          ").out (lowercase_decl_name).out ("_type (type)\n"
			   "    {\n"
			   "    }\n"
			   "};\n"
			   "\n");

		{
		    List< Ref<Declaration_Phrases::PhraseRecord> >::DataIterator phrase_iter (decl_phrases->phrases);
		    Size phrase_index = 0;
		    while (!phrase_iter.done ()) {
			Ref<Declaration_Phrases::PhraseRecord> &phrase_record = phrase_iter.next ();
			Phrase *phrase = phrase_record->phrase;

			Ref<String> phrase_name;
			if (phrase->phrase_name.isNull ()) {
			    phrase_name = String::forPrintTask (Pt ("phrase") (phrase_index));
			    phrase_index ++;
			} else
			    phrase_name = phrase->phrase_name;

			compileHeader_Phrase_External (file, phrase_record, opts, ConstMemoryDesc (decl_name, countStrLength (decl_name)));

			file->out ("class ").out (opts->capital_header_name).out ("_").out (decl_name).out ("_"
					   ).out (phrase_name).out (" : "
					   "public ").out (opts->capital_header_name).out ("_").out (decl_name).out ("\n"
				   "{\n"
				   "public:\n");

			compileHeader_Phrase (file,
					      phrase_record,
					      opts,
					      ConstMemoryDesc (decl_name, countStrLength (decl_name)));

			file->out ("\n"
				   "    ").out (opts->capital_header_name).out ("_").out (decl_name).out ("_").out (phrase_name).out (" ()\n"
				   "        : ").out (opts->capital_header_name).out ("_").out (decl_name).out (" ("
						    ).out (opts->capital_header_name).out ("_").out (decl_name).out ("::"
							    "t_").out (phrase_name).out (")\n");

			compileHeader_Phrase (file,
					      phrase_record,
					      opts,
					      ConstMemoryDesc (decl_name, countStrLength (decl_name)),
					      true /* initializers_mode */);

			file->out ("    {\n"
				   "    }\n"
				   "};\n"
				   "\n");
		    }
		}
	    } else {
		if (decl_phrases->phrases.first != NULL)
		    compileHeader_Phrase (file,
					  decl_phrases->phrases.first->data,
					  opts,
					  ConstMemoryDesc (decl_name, countStrLength (decl_name)));

		file->out ("\n"
			   "    ").out (opts->capital_header_name).out ("_").out (decl_name).out (" ()\n"
			   "        : ").out (opts->capital_header_name).out ("Element ("
					    ).out (opts->capital_header_name).out ("Element::t_").out (decl_name).out (")\n");

		if (decl_phrases->phrases.first != NULL)
		    compileHeader_Phrase (file,
					  decl_phrases->phrases.first->data,
					  opts,
					  ConstMemoryDesc (decl_name, countStrLength (decl_name)),
					  true /* initializers_mode */);

		file->out ("    {\n"
			   "    }\n"
			   "};\n"
			   "\n");
	    }

	    if (!compareStrings (decl->declaration_name->getData (), "*")) {
		file->out ("void dump_").out (opts->header_name).out ("_").out (decl->declaration_name).out (" ("
				   ).out (opts->capital_header_name).out ("_").out (decl->declaration_name).out (" const *, "
				   "MyCpp::Size nest_level = 0);\n\n");
	    }
	}
    }

    if (got_global_grammar) {
	file->out ("MyCpp::Ref<Pargen::Grammar> create_").out (opts->header_name).out ("_grammar ();\n"
		   "\n"
		   "void dump_").out (opts->header_name).out ("_grammar (").out (opts->capital_header_name).out ("_Grammar const *el);\n"
		   "\n");
    }

    file->out ("}\n"
	       "\n");

    file->out ("#endif /* __").out (opts->all_caps_module_name).out ("__").out (opts->all_caps_header_name).out ("_PARGEN_H__ */\n"
	       "\n");

}

}


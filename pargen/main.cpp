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


#include <cstdlib>

#include <mycpp/mycpp.h>
#include <mycpp/util.h>
#include <mycpp/cmdline.h>
#include <mycpp/io.h>
#include <mycpp/file.h>

#include <mylang/file_token_stream.h>
#include <mylang/util.h>

#include <pargen/util.h>
#include <pargen/pargen_task_parser.h>
#include <pargen/header_compiler.h>
#include <pargen/source_compiler.h>


#define DEBUG(a) a
#define DEBUG_OLD(a) ;

#define FUNC_NAME(a) a


using namespace MyCpp;
using namespace MyLang;
using namespace Pargen;

struct Options
{
    Ref<String> module_name;
    Ref<String> namespace_name;
    Ref<String> header_name;

    Bool extmode;

    Bool help;
};

Options options;

static void
print_usage ()
{
    errf->print ("Usage: pargen [options] <file>\n"
		 "Options:\n"
		 "  --module-name\n"
                 "  --namespace\n"
		 "  --header-name\n"
		 "  --extmode\n"
		 "  -h, --help")
	 .pendl ();
}

static bool
cmdline_module_name (const char * /* short_name */,
		     const char * /* long_name */,
		     const char *value,
		     void * /* opt_data */,
		     void * /* callback_data */)
{
    options.module_name = String::forData (value);
    return true;
}

static bool
cmdline_namespace_name (const char * /* short_name */,
                        const char * /* long_name */,
                        const char *value,
                        void       * /* opt_data */,
                        void       * /* callback_data */)
{
    options.namespace_name = String::forData (value);
    return true;
}

static bool
cmdline_header_name (const char * /* short_name */,
		     const char * /* long_name */,
		     const char *value,
		     void       * /* opt_data */,
		     void       * /* callback_data */)
{
    options.header_name = String::forData (value);
    return true;
}

static bool
cmdline_help (const char * /* short_name */,
	      const char * /* long_name */,
	      const char * /* value */,
	      void       * /* opt_data */,
	      void       * /* callback_data */)
{
    options.help = true;
    return true;
}

static bool
cmdline_extmode (const char * /* short_name */,
		 const char * /* long_name */,
		 const char * /* value */,
		 void       * /* opt_data */,
		 void       * /* callback_data */)
{
    options.extmode = true;
    return true;
}

class PargenCharacterRecognizer : public CharacterRecognizer
{
public:
    bool isAlphanumeric (Unichar c)
    {
	return CharacterRecognizer::isAlphanumeric (c) || c == (Unichar) '-';
    }

    bool isAlpha (Unichar c)
    {
	return CharacterRecognizer::isAlpha (c) || c == (Unichar) '-';
    }
};

int main (int argc, char **argv)
{
    myCppInit ();

    {
	const Size num_opts = 5;
	CmdlineOption opts [num_opts];

	opts [0].short_name = NULL;
	opts [0].long_name  = "module-name";
	opts [0].with_value = true;
	opts [0].opt_data   = NULL;
	opts [0].opt_callback = cmdline_module_name;

        opts [1].short_name = NULL;
        opts [1].long_name  = "namespace";
        opts [1].with_value = true;
        opts [1].opt_data   = NULL;
        opts [1].opt_callback = cmdline_namespace_name;

	opts [2].short_name = NULL;
	opts [2].long_name  = "header-name";
	opts [2].with_value = true;
	opts [2].opt_data   = NULL;
	opts [2].opt_callback = cmdline_header_name;

	opts [3].short_name = "h";
	opts [3].long_name  = "help";
	opts [3].with_value = false;
	opts [3].opt_data   = NULL;
	opts [3].opt_callback = cmdline_help;

	opts [4].short_name = NULL;
	opts [4].long_name  = "extmode";
	opts [4].with_value = false;
	opts [4].opt_data   = NULL;
	opts [4].opt_callback = cmdline_extmode;

	ArrayIterator<CmdlineOption> opts_iter (opts, num_opts);
	parseCmdline (&argc, &argv, opts_iter,
		      NULL /* callback */,
		      NULL /* callbackData */);
    }

    if (options.help) {
	print_usage ();
	return 0;
    }

    if (argc != 2)
    {
	print_usage ();
	return EXIT_FAILURE;
    }

    const char *input_filename = argv [1];

    Ref<File> file;
    Bool file_closed;

    Ref<File> header_file;
    Bool header_file_closed;

    Ref<File> source_file;
    Bool source_file_closed;

    try {
	if (argc < 2) {
	    errf->print ("File not specified").pendl ();
	    return EXIT_FAILURE;
	}

	if (options.module_name.isNull()) {
	    errf->print ("Module name not specified").pendl ();
	    return EXIT_FAILURE;
	}

        if (options.namespace_name.isNull())
            options.namespace_name = options.module_name;

	if (options.header_name.isNull()) {
	    errf->print ("Header name not specified").pendl ();
	    return EXIT_FAILURE;
	}

	Ref<String> header_filename = String::forPrintTask (Pt (options.header_name) ("_pargen.h"));
	Ref<String> source_filename = String::forPrintTask (Pt (options.header_name) ("_pargen.cpp"));

	Ref<CompilationOptions> comp_opts = grab (new CompilationOptions);

	comp_opts->module_name = options.module_name;
	comp_opts->capital_module_name =
		options.extmode ? comp_opts->module_name :
			capitalizeName (comp_opts->module_name->getMemoryDesc (),
					false /* keep_underscore */);
	comp_opts->all_caps_module_name = capitalizeNameAllCaps (comp_opts->module_name->getMemoryDesc ());

        comp_opts->capital_namespace_name =
                options.extmode ? options.namespace_name :
                        capitalizeName (options.namespace_name->getMemoryDesc(),
                                        false /* keep_underscore */);

	comp_opts->header_name = options.header_name;
	comp_opts->capital_header_name =
		options.extmode ? comp_opts->module_name :
			capitalizeName (comp_opts->header_name->getMemoryDesc (),
					false /* keep_underscore */);
	comp_opts->all_caps_header_name = capitalizeNameAllCaps (comp_opts->header_name->getMemoryDesc ());

	file = File::createDefault (input_filename,
				    0 /* open_flags */,
				    AccessMode::ReadOnly);
	Ref<FileTokenStream> file_token_stream = grab (new FileTokenStream (file, grab (new PargenCharacterRecognizer ()), true /* report_newlines */));

	Ref<PargenTask> pargen_task = parsePargenTask (file_token_stream);
	DEBUG_OLD (
	    dumpDeclarations (pargen_task);
	)

	file_closed = true;
	file->close (true /* flush_data */);
	file = NULL;

	header_file = File::createDefault (header_filename->getData (),
					   OpenFlags::Create | OpenFlags::Truncate,
					   AccessMode::ReadWrite);
	compileHeader (header_file, pargen_task, comp_opts);

	header_file_closed = true;
	header_file->close (true /* flush_data */);
	header_file = NULL;

	source_file = File::createDefault (source_filename->getData (),
					   OpenFlags::Create | OpenFlags::Truncate,
					   AccessMode::ReadWrite);
	compileSource (source_file, pargen_task, comp_opts);

	source_file_closed = true;
	source_file->close (true /* flush_data */);
	source_file = NULL;

	return 0;

    } catch (ParsingException &exc) {
	abortIf (exc.fpos.char_pos < exc.fpos.line_pos);
	errf->print ("Parsing exception "
		     "at line ").print (exc.fpos.line + 1).print (", "
		     "character ").print ((exc.fpos.char_pos - exc.fpos.line_pos) + 1)
	     .pendl ()
	     .pendl ();

	printErrorContext (errf, file, exc.fpos);

	errf->pendl ();
	printException (errf, exc);
    } catch (Exception &exc) {
	printException (errf, exc);
    }

    if (!file.isNull () &&
	!file_closed)
    {
	try {
	    file->close (true /* flush_data */);
	} catch (Exception &exc) {
	    printException (errf, exc);
	}
    }

    if (!header_file.isNull () &&
	!header_file_closed)
    {
	try {
	    header_file->close (true /* flush_data */);
	} catch (Exception &exc) {
	    printException (errf, exc);
	}
    }

    if (!source_file.isNull () &&
	!source_file_closed)
    {
	try {
	    source_file->close (true /* flush_data */);
	} catch (Exception &exc) {
	    printException (errf, exc);
	}
    }

    return EXIT_FAILURE;
}


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


#ifndef __PARGEN__ACCEPTOR_H__
#define __PARGEN__ACCEPTOR_H__

#include <typeinfo>

#include <mycpp/object.h>
#include <mycpp/util.h>
#include <mycpp/io.h>

#include <pargen/parser_element.h>


#define DEBUG(a) ;


namespace Pargen {

using namespace MyCpp;

class Acceptor : public virtual SimplyReferenced
{
public:
    virtual void setParserElement (ParserElement *parser_element) = 0;
};

template <class T>
class ListAcceptor : public Acceptor
{
protected:
    List<T*> *target_list;

public:
    void setParserElement (ParserElement *parser_element)
    {
	if (parser_element == NULL)
	    return;

	abortIf (parser_element == NULL);
//	abortIf (!T::testType (parser_element));
//	abortIf (typeid (parser_element) != typeid (T));
	abortIf (dynamic_cast <T*> (parser_element) == NULL);

	if (target_list != NULL)
	    target_list->append (static_cast <T*> (parser_element));
    }

    void init (List<T*> *target_list)
    {
	this->target_list = target_list;
    }

    ListAcceptor (List<T*> *target_list)
    {
	this->target_list = target_list;
    }

    ListAcceptor ()
    {
    }
};
#if 0
template <class T>
class ListAcceptor : public Acceptor
{
protected:
    List< Ref<T> > *target_list;

public:
    void setParserElement (ParserElement *parser_element)
    {
	if (parser_element == NULL)
	    return;

	abortIf (parser_element == NULL);
//	abortIf (!T::testType (parser_element));
//	abortIf (typeid (parser_element) != typeid (T));
	abortIf (dynamic_cast <T*> (parser_element) == NULL);

	if (target_list != NULL)
	    target_list->append (static_cast <T*> (parser_element));
    }

    void init (List< Ref<T> > *target_list)
    {
	this->target_list = target_list;
    }

    ListAcceptor (List< Ref<T> > *target_list)
    {
	this->target_list = target_list;
    }

    ListAcceptor ()
    {
    }
};
#endif

template <class T>
class PtrAcceptor : public Acceptor
{
protected:
    T **target_ptr;

public:
    void setParserElement (ParserElement * const parser_element)
    {
	DEBUG (
	    errf->print ("PtrAcceptor.setParserElement: acceptor 0x").printHex ((Uint64) this).print ("', parser_element 0x").printHex ((Uint64) parser_element).pendl ();
	)

	if (parser_element == NULL) {
	    if (target_ptr != NULL)
		*target_ptr = NULL;

	    return;
	}

	abortIf (parser_element == NULL);
	abortIf (!T::testType (parser_element));
	abortIf (target_ptr != NULL && (*target_ptr) != NULL);

	if (target_ptr != NULL)
	    *target_ptr = parser_element;
    }

    void init (T ** const target_ptr)
    {
	this->target_ptr = target_ptr;

	if (target_ptr != NULL)
	    *target_ptr = NULL;
    }

    PtrAcceptor (T ** const target_ptr)
    {
	this->target_prt = target_ptr;

	if (target_ptr != NULL)
	    *target_ptr = NULL;
    }

    PtrAcceptor ()
    {
    }
};

template <class T>
class RefAcceptor : public Acceptor
{
protected:
    Ref<T> *target_ref;

public:
    void setParserElement (ParserElement *parser_element)
    {
	DEBUG (
	    errf->print ("RefAcceptor.setParserElement: acceptor 0x").printHex ((Uint64) this).print ("', parser_element 0x").printHex ((Uint64) parser_element).pendl ();
	)

	if (parser_element == NULL) {
	    if (target_ref != NULL)
		*target_ref = NULL;

	    return;
	}

	abortIf (parser_element == NULL);
	abortIf (!T::testType (parser_element));
	abortIf (target_ref != NULL && !target_ref->isNull ());

	if (target_ref != NULL)
	    *target_ref = parser_element;
    }

    void init (Ref<T> *target_ref)
    {
	this->target_ref = target_ref;

	if (target_ref != NULL)
	    *target_ref = NULL;
    }

    RefAcceptor (Ref<T> *target_ref)
    {
	this->target_ref = target_ref;

	if (target_ref != NULL)
	    *target_ref = NULL;
    }

    RefAcceptor ()
    {
    }
};

}


#undef DEBUG


#endif /* __PARGEN__ACCEPTOR_H__ */


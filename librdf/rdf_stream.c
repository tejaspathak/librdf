/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rdf_stream.c - RDF Statement Stream Implementation
 *
 * $Id$
 *
 * Copyright (C) 2000-2001 David Beckett - http://purl.org/net/dajobe/
 * Institute for Learning and Research Technology - http://www.ilrt.org/
 * University of Bristol - http://www.bristol.ac.uk/
 * 
 * This package is Free Software or Open Source available under the
 * following licenses (these are alternatives):
 *   1. GNU Lesser General Public License (LGPL)
 *   2. GNU General Public License (GPL)
 *   3. Mozilla Public License (MPL)
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * full license terms.
 * 
 * 
 */


#include <rdf_config.h>

#include <stdio.h>
#include <sys/types.h>

#include <librdf.h>
#include <rdf_stream.h>


/* prototypes of local helper functions */
static librdf_statement* librdf_stream_get_next_mapped_statement(librdf_stream* stream);


/**
 * librdf_new_stream - Constructor - create a new librdf_stream
 * @context: context to pass to the stream implementing objects
 * @end_of_stream: pointer to function to test for end of stream
 * @next_statement: pointer to function to get the next statement in stream
 * @finished: pointer to function to finish the stream.
 *
 * Creates a new stream with an implementation based on the passed in
 * functions.  The functions next_statement and end_of_stream will be called
 * multiple times until either of them signify the end of stream by
 * returning NULL or non 0 respectively.  The finished function is called
 * once only when the stream object is destroyed with librdf_free_stream()
 *
 * A mapping function can be set for the stream using librdf_stream_set_map()
 * function which allows the statements generated by the stream to be
 * filtered and/or altered as they are generated before passing back
 * to the user.
 *
 * Return value:  a new &librdf_stream object or NULL on failure
 **/
librdf_stream*
librdf_new_stream(librdf_world *world, 
                  void* context,
		  int (*end_of_stream)(void*),
		  librdf_statement* (*next_statement)(void*),
		  void (*finished)(void*))
{
  librdf_stream* new_stream;
  
  new_stream=(librdf_stream*)LIBRDF_CALLOC(librdf_stream, 1, 
					   sizeof(librdf_stream));
  if(!new_stream)
    return NULL;


  new_stream->context=context;

  new_stream->end_of_stream=end_of_stream;
  new_stream->next_statement=next_statement;
  new_stream->finished=finished;

  new_stream->is_end_stream=0;
  
  return new_stream;
}


/**
 * librdf_free_stream - Destructor - destroy an libdf_stream object
 * @stream: &librdf_stream object
 **/
void
librdf_free_stream(librdf_stream* stream) 
{
  stream->finished(stream->context);
  if(stream->next)
    librdf_free_statement(stream->next);
  
  LIBRDF_FREE(librdf_stream, stream);
}


/*
 * librdf_stream_get_next_mapped_element - helper function to get the next element with map appled
 * @stream: &librdf_stream object
 * 
 * A helper function that gets the next element subject to the user
 * defined map function, if set by librdf_stream_set_map(),
 * 
 * Return value: the next statement or NULL at end of stream
 */
static librdf_statement*
librdf_stream_get_next_mapped_statement(librdf_stream* stream) 
{
  librdf_statement* statement=NULL;
  
  /* find next statement subject to map */
  while(!stream->end_of_stream(stream->context)) {
    statement=stream->next_statement(stream->context);
    if(!statement)
      break;
    
    /* apply the map to the statement  */
    statement=stream->map(stream->map_context, statement);
    /* found something, return it */
    if(statement)
      break;
  }
  return statement;
}


/**
 * librdf_stream_next - Get the next librdf_statement in the stream
 * @stream: &librdf_stream object
 *
 * The returned statement is owned by the caller and must be freed
 * using librdf_free_statement().
 *
 * Return value: a new &librdf_statement object or NULL at end of stream.
 **/
librdf_statement*
librdf_stream_next(librdf_stream* stream) 
{
  librdf_statement* statement;

  if(stream->is_end_stream)
    return NULL;

  /* simple case, no mapping so pass on */
  if(!stream->map)
    return stream->next_statement(stream->context);

  /* mapping from here */

  /* return stored element if there is one */
  if(stream->next) {
    statement=stream->next;
    stream->next=NULL;
    return statement;
  }

  /* else get a new one or NULL at end of list */
  statement=librdf_stream_get_next_mapped_statement(stream);
  if(!statement)
    stream->is_end_stream=1;

  return statement;
}


/**
 * librdf_stream_end - Test if the stream has ended
 * @stream: &librdf_stream object
 * 
 * Return value: non 0 at end of stream.
 **/
int
librdf_stream_end(librdf_stream* stream) 
{
  /* always end of NULL stream */
  if(!stream)
    return 1;
  
  if(stream->is_end_stream)
    return 1;

  /* simple case, no mapping so pass on */
  if(!stream->map)
    return (stream->is_end_stream=stream->end_of_stream(stream->context));


  /* mapping from here */

  /* already have 1 stored item */
  if(stream->next)
    return 0;

  /* get next item subject to map or NULL if list ended */
  stream->next=librdf_stream_get_next_mapped_statement(stream);
  if(!stream->next)
    stream->is_end_stream=1;
  
  return stream->is_end_stream;
}


/**
 * librdf_stream_set_map - Set the filtering/mapping function for the stream
 * @stream: &librdf_stream object
 * @map: mapping function.
 * @map_context: context
 * 
 * The function 
 * is called with the mapping context and the next statement.  The return
 * value of the mapping function is then passed on to the user, if not NULL.
 * If NULL is returned, that statement is not emitted.
 **/
void
librdf_stream_set_map(librdf_stream* stream, 
		      librdf_statement* (*map)(void* context, librdf_statement* statement), 
		      void* map_context) 
{
  stream->map=map;
  stream->map_context=map_context;
}



static int librdf_stream_from_node_iterator_end_of_stream(void* context);
static librdf_statement* librdf_stream_from_node_iterator_next_statement(void* context);
static void librdf_stream_from_node_iterator_finished(void* context);

typedef struct {
  librdf_iterator *iterator;
  librdf_statement* statement;
  unsigned int field;
} librdf_stream_from_node_iterator_stream_context;



/**
 * librdf_new_stream_from_node_iterator - Constructor - create a new librdf_stream from an iterator of nodes
 * @iterator: &librdf_iterator of &librdf_node objects
 * @statement: &librdf_statement prototype with one NULL node space
 * @field: node part of statement
 *
 * Creates a new &librdf_stream using the passed in &librdf_iterator
 * which generates a series of &librdf_node objects.  The resulting
 * nodes are then inserted into the given statement and returned.
 * The field attribute indicates which statement node is being generated.
 *
 * Return value: a new &librdf_stream object or NULL on failure
 **/
librdf_stream*
librdf_new_stream_from_node_iterator(librdf_iterator* iterator,
                                     librdf_statement* statement,
                                     unsigned int field)
{
  librdf_stream_from_node_iterator_stream_context *scontext;
  librdf_stream *stream;

  scontext=(librdf_stream_from_node_iterator_stream_context*)LIBRDF_CALLOC(librdf_stream_from_node_iterator_stream_context, 1, sizeof(librdf_stream_from_node_iterator_stream_context));
  if(!scontext)
    return NULL;

  scontext->iterator=iterator;
  scontext->statement=statement;
  scontext->field=field;
  
  stream=librdf_new_stream(iterator->world,
                           (void*)scontext,
                           &librdf_stream_from_node_iterator_end_of_stream,
                           &librdf_stream_from_node_iterator_next_statement,
                           &librdf_stream_from_node_iterator_finished);
  if(!stream) {
    librdf_stream_from_node_iterator_finished((void*)scontext);
    return NULL;
  }
  
  return stream;  
}


static int
librdf_stream_from_node_iterator_end_of_stream(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;

  return !librdf_iterator_have_elements(scontext->iterator);
}


static librdf_statement*
librdf_stream_from_node_iterator_next_statement(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;
  librdf_node* node;
  librdf_statement* statement;
  
  if(!(node=(librdf_node*)librdf_iterator_get_next(scontext->iterator)))
    return NULL;

  statement=librdf_new_statement_from_statement(scontext->statement);
  if(!statement) {
    librdf_free_node(node);
    return NULL;
  }

  switch(scontext->field) {
    case LIBRDF_STATEMENT_SUBJECT:
      librdf_statement_set_subject(statement, node);
      break;
    case LIBRDF_STATEMENT_PREDICATE:
      librdf_statement_set_predicate(statement, node);
      break;
    case LIBRDF_STATEMENT_OBJECT:
      librdf_statement_set_object(statement, node);
      break;
    default:
      LIBRDF_FATAL2(librdf_stream_from_node_iterator_next_statement,
                    "Illegal statement field %d seen\n", scontext->field);

  }

  return statement;
}


static void
librdf_stream_from_node_iterator_finished(void* context)
{
  librdf_stream_from_node_iterator_stream_context* scontext=(librdf_stream_from_node_iterator_stream_context*)context;
  
  if(scontext->iterator)
    librdf_free_iterator(scontext->iterator);

  LIBRDF_FREE(librdf_stream_from_node_iterator_stream_context, scontext);
}



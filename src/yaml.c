
#include <stdio.h>
#include <yaml.h>
#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>


void mrb_mruby_yaml_gem_init(mrb_state *mrb);
void mrb_mruby_yaml_gem_final(mrb_state *mrb);

typedef struct yaml_write_data_t
{
	mrb_state *mrb;
	mrb_value str;
} yaml_write_data_t;

int yaml_write(void *data, unsigned char *buffer, size_t size);


static mrb_value yaml_node_to_mrb(mrb_state *mrb,
	yaml_document_t *document, yaml_node_t *node);
static int yaml_mrb_to_node(mrb_state *mrb,
	yaml_document_t *document, mrb_value value);


void
mruby_test()
{
	mrb_state *mrb = mrb_open();
	mrb_mruby_yaml_gem_init(mrb);
	
	/* Launch the test script */
	FILE *file = fopen("test.rb", "r");
	mrb_load_file(mrb, file);
	
	mrb_mruby_yaml_gem_final(mrb);
	mrb_close(mrb);
}


int
main(int argc, char *argv[])
{
	mruby_test();
	
	return EXIT_SUCCESS;
}


mrb_value
mrb_yaml_load(mrb_state *mrb, mrb_value self)
{
	yaml_parser_t parser;
	yaml_document_t document;
	yaml_node_t *root;
	
	mrb_value yaml_str;
	mrb_value result;
	
	/* Extract arguments */
	mrb_get_args(mrb, "S", &yaml_str);
	
	/* Initialize the YAML parser */
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser,
		RSTRING_PTR(yaml_str), RSTRING_LEN(yaml_str));
	
	/* Load the document */
	yaml_parser_load(&parser, &document);
	
	/* Error handling */
	if (parser.error != YAML_NO_ERROR)
	{
		mrb_raise(mrb, E_RUNTIME_ERROR, parser.problem);
		return mrb_nil_value();
	}
	
	/* Convert the root node to an MRuby value */
	root = yaml_document_get_root_node(&document);
	result = yaml_node_to_mrb(mrb, &document, root);
	
	/* Clean up */
	yaml_document_delete(&document);
	yaml_parser_delete(&parser);
	
	return result;
}


mrb_value
mrb_yaml_dump(mrb_state *mrb, mrb_value self)
{
	yaml_emitter_t emitter;
	yaml_document_t document;
	
	mrb_value root;
	yaml_write_data_t write_data;
	
	/* Extract arguments */
	mrb_get_args(mrb, "o", &root);
	
	/* Build the document */
	yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0);
	yaml_mrb_to_node(mrb, &document, root);
	
	/* Initialize the emitter */
	yaml_emitter_initialize(&emitter);
	
	write_data.mrb = mrb;
	write_data.str = mrb_str_new(mrb, NULL, 0);
	yaml_emitter_set_output(&emitter, &yaml_write, &write_data);
	
	/* Dump the document */
	yaml_emitter_open(&emitter);
	yaml_emitter_dump(&emitter, &document);
	yaml_emitter_close(&emitter);
	
	/* Clean up */
	yaml_emitter_delete(&emitter);
	yaml_document_delete(&document);
	
	return write_data.str;
}


int yaml_write(void *data, unsigned char *buffer, size_t size)
{
	yaml_write_data_t *write_data = (yaml_write_data_t *) data;
	mrb_str_buf_cat(write_data->mrb, write_data->str, buffer, size);
	return 1;
}


mrb_value
yaml_node_to_mrb(mrb_state *mrb,
	yaml_document_t *document, yaml_node_t *node)
{
	switch (node->type)
	{
		case YAML_SCALAR_NODE:
		{
			/* Every scalar is a string */
			mrb_value result = mrb_str_new(mrb, node->data.scalar.value,
				node->data.scalar.length);
			return result;
		}
		
		case YAML_SEQUENCE_NODE:
		{
			/* Sequences are arrays in Ruby */
			mrb_value result = mrb_ary_new(mrb);
			yaml_node_item_t *item;
			
			int ai = mrb_gc_arena_save(mrb);
			
			for (item = node->data.sequence.items.start;
				item < node->data.sequence.items.top; item++)
			{
				yaml_node_t *child_node = yaml_document_get_node(document, *item);
				mrb_value child = yaml_node_to_mrb(mrb, document, child_node);
				
				mrb_ary_push(mrb, result, child);
				mrb_gc_arena_restore(mrb, ai);
			}
			
			return result;
		}
		
		case YAML_MAPPING_NODE:
		{
			/* Mappings are hashes in Ruby */
			mrb_value result = mrb_hash_new(mrb);
			yaml_node_t *key_node;
			yaml_node_t *value_node;
			yaml_node_pair_t *pair;
			
			int ai = mrb_gc_arena_save(mrb);
			
			for (pair = node->data.mapping.pairs.start;
				pair < node->data.mapping.pairs.top; pair++)
			{
				key_node = yaml_document_get_node(document, pair->key);
				value_node = yaml_document_get_node(document, pair->value);
				
				mrb_value key = yaml_node_to_mrb(mrb, document, key_node);
				mrb_value value = yaml_node_to_mrb(mrb, document, value_node);
				
				mrb_hash_set(mrb, result, key, value);
				mrb_gc_arena_restore(mrb, ai);
			}
			
			return result;
		}
		
		default:
			return mrb_nil_value();
	}
}


int yaml_mrb_to_node(mrb_state *mrb,
	yaml_document_t *document, mrb_value value)
{
	int node;
	
	switch (mrb_type(value))
	{
		case MRB_TT_ARRAY:
		{
			mrb_int len = mrb_ary_len(mrb, value);
			mrb_int i;
			int ai = mrb_gc_arena_save(mrb);
			
			node = yaml_document_add_sequence(document, NULL,
				YAML_ANY_SEQUENCE_STYLE);
			
			for (i = 0; i < len; i++)
			{
				mrb_value child = mrb_ary_ref(mrb, value, i);
				int child_node = yaml_mrb_to_node(mrb, document, child);
				
				/* Add the child to the sequence */
				yaml_document_append_sequence_item(document, node, child_node);
				mrb_gc_arena_restore(mrb, ai);
			}
			
			break;
		}
		
		case MRB_TT_HASH:
		{
			/* Iterating a list of keys is slow, but it only
			 * requires use of the interface defined in `hash.h`.
			 */
			
			mrb_value keys = mrb_hash_keys(mrb, value);
			mrb_int len = mrb_ary_len(mrb, keys);
			mrb_int i;
			int ai = mrb_gc_arena_save(mrb);
			
			node = yaml_document_add_mapping(document, NULL,
				YAML_ANY_MAPPING_STYLE);
			
			for (i = 0; i < len; i++)
			{
				mrb_value key = mrb_ary_ref(mrb, keys, i);
				mrb_value child = mrb_hash_get(mrb, value, key);
				
				int key_node = yaml_mrb_to_node(mrb, document, key);
				int child_node = yaml_mrb_to_node(mrb, document, child);
				
				/* Add the key/value pair to the mapping */
				yaml_document_append_mapping_pair(document, node,
					key_node, child_node);
				mrb_gc_arena_restore(mrb, ai);
			}
			
			break;
		}
		
		default:
		{
			/* Equivalent to `obj = obj#to_s` */
			value = mrb_obj_as_string(mrb, value);
			
			/* Fallthrough */
		}
		
		case MRB_TT_STRING:
		{
			yaml_char_t *value_chars = RSTRING_PTR(value);
			node = yaml_document_add_scalar(document, NULL,
				value_chars, RSTRING_LEN(value), YAML_ANY_SCALAR_STYLE);
			break;
		}
	}
	
	return node;
}


void
mrb_mruby_yaml_gem_init(mrb_state *mrb)
{
	struct RClass *klass = mrb_define_module(mrb, "YAML");
	mrb_define_class_method(mrb, klass, "load", mrb_yaml_load, ARGS_REQ(1));
	mrb_define_class_method(mrb, klass, "dump", mrb_yaml_dump, ARGS_REQ(1));
}


void
mrb_mruby_yaml_gem_final(mrb_state *mrb)
{
}

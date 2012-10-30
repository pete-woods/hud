#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pronounce-dict.h"

#define DICT_PATH "/"

struct _PronounceDictPrivate {
	GHashTable * dict;
};

#define PRONOUNCE_DICT_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), PRONOUNCE_DICT_TYPE, PronounceDictPrivate))

static void pronounce_dict_class_init (PronounceDictClass *klass);
static void pronounce_dict_init       (PronounceDict *self);
static void pronounce_dict_dispose    (GObject *object);
static void pronounce_dict_finalize   (GObject *object);

G_DEFINE_TYPE (PronounceDict, pronounce_dict, G_TYPE_OBJECT);

/* Build up our class */
static void
pronounce_dict_class_init (PronounceDictClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (PronounceDictPrivate));

	object_class->dispose = pronounce_dict_dispose;
	object_class->finalize = pronounce_dict_finalize;

	return;
}

/* We've got a list of strings, let's free it */
static void
str_list_free (gpointer data)
{
	GList * list = (GList *)data;
	g_list_free_full(list, g_free);

	return;
}

/* Initialize stuff */
static void
pronounce_dict_init (PronounceDict *self)
{
	self->priv = PRONOUNCE_DICT_GET_PRIVATE(self);

	self->priv->dict = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, str_list_free);

	return;
}

/* Drop references */
static void
pronounce_dict_dispose (GObject *object)
{

	G_OBJECT_CLASS (pronounce_dict_parent_class)->dispose (object);
	return;
}

/* Clean up memory */
static void
pronounce_dict_finalize (GObject *object)
{
	PronounceDict * dict = PRONOUNCE_DICT(object);

	g_hash_table_unref(dict->priv->dict);

	G_OBJECT_CLASS (pronounce_dict_parent_class)->finalize (object);
	return;
}

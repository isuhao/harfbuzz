/*
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#include "options.hh"

#if HAVE_FREETYPE
#include <hb-ft.h>
#endif


bool debug = FALSE;

static gchar *
shapers_to_string (void)
{
  GString *shapers = g_string_new (NULL);
  const char **shaper_list = hb_shape_list_shapers ();

  for (; *shaper_list; shaper_list++) {
    g_string_append (shapers, *shaper_list);
    g_string_append_c (shapers, ',');
  }
  g_string_truncate (shapers, MAX (0, (gint)shapers->len - 1));

  return g_string_free (shapers, FALSE);
}

static G_GNUC_NORETURN gboolean
show_version (const char *name G_GNUC_UNUSED,
	      const char *arg G_GNUC_UNUSED,
	      gpointer    data G_GNUC_UNUSED,
	      GError    **error G_GNUC_UNUSED)
{
  g_printf ("%s (%s) %s\n", g_get_prgname (), PACKAGE_NAME, PACKAGE_VERSION);

  char *shapers = shapers_to_string ();
  g_printf ("Available shapers: %s\n", shapers);
  g_free (shapers);
  if (strcmp (HB_VERSION_STRING, hb_version_string ()))
    g_printf ("Linked HarfBuzz library has a different version: %s\n", hb_version_string ());

  exit(0);
}


void
option_parser_t::add_main_options (void)
{
  GOptionEntry entries[] =
  {
    {"version",		0, G_OPTION_FLAG_NO_ARG,
			      G_OPTION_ARG_CALLBACK,	(gpointer) &show_version,	"Show version numbers",			NULL},
    {"debug",		0, 0, G_OPTION_ARG_NONE,	&debug,				"Free all resources before exit",	NULL},
    {NULL}
  };
  g_option_context_add_main_entries (context, entries, NULL);
}

static gboolean
pre_parse (GOptionContext *context G_GNUC_UNUSED,
	   GOptionGroup *group G_GNUC_UNUSED,
	   gpointer data,
	   GError **error)
{
  option_group_t *option_group = (option_group_t *) data;
  option_group->pre_parse (error);
  return *error == NULL;
}

static gboolean
post_parse (GOptionContext *context G_GNUC_UNUSED,
	    GOptionGroup *group G_GNUC_UNUSED,
	    gpointer data,
	    GError **error)
{
  option_group_t *option_group = static_cast<option_group_t *>(data);
  option_group->post_parse (error);
  return *error == NULL;
}

void
option_parser_t::add_group (GOptionEntry   *entries,
			    const gchar    *name,
			    const gchar    *description,
			    const gchar    *help_description,
			    option_group_t *option_group)
{
  GOptionGroup *group = g_option_group_new (name, description, help_description,
					    static_cast<gpointer>(option_group), NULL);
  g_option_group_add_entries (group, entries);
  g_option_group_set_parse_hooks (group, pre_parse, post_parse);
  g_option_context_add_group (context, group);
}

void
option_parser_t::parse (int *argc, char ***argv)
{
  GError *parse_error = NULL;
  if (!g_option_context_parse (context, argc, argv, &parse_error))
  {
    if (parse_error != NULL)
      fail (TRUE, "%s", parse_error->message);
    else
      fail (TRUE, "Option parse error");
  }
}


static gboolean
parse_margin (const char *name G_GNUC_UNUSED,
	      const char *arg,
	      gpointer    data,
	      GError    **error G_GNUC_UNUSED)
{
  view_options_t *view_opts = (view_options_t *) data;
  view_options_t::margin_t &m = view_opts->margin;
  switch (sscanf (arg, "%lf %lf %lf %lf", &m.t, &m.r, &m.b, &m.l)) {
    case 1: m.r = m.t;
    case 2: m.b = m.t;
    case 3: m.l = m.r;
    case 4: return TRUE;
    default:
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
		   "%s argument should be one to four space-separated numbers",
		   name);
      return FALSE;
  }
}


static gboolean
parse_shapers (const char *name G_GNUC_UNUSED,
	       const char *arg,
	       gpointer    data,
	       GError    **error G_GNUC_UNUSED)
{
  shape_options_t *shape_opts = (shape_options_t *) data;
  shape_opts->shapers = g_strsplit (arg, ",", 0);
  return TRUE;
}


static void
parse_space (char **pp)
{
  char c;
#define ISSPACE(c) ((c)==' '||(c)=='\f'||(c)=='\n'||(c)=='\r'||(c)=='\t'||(c)=='\v')
  while (c = **pp, ISSPACE (c))
    (*pp)++;
#undef ISSPACE
}

static hb_bool_t
parse_char (char **pp, char c)
{
  parse_space (pp);

  if (**pp != c)
    return FALSE;

  (*pp)++;
  return TRUE;
}

static hb_bool_t
parse_uint (char **pp, unsigned int *pv)
{
  char *p = *pp;
  unsigned int v;

  v = strtol (p, pp, 0);

  if (p == *pp)
    return FALSE;

  *pv = v;
  return TRUE;
}


static hb_bool_t
parse_feature_value_prefix (char **pp, hb_feature_t *feature)
{
  if (parse_char (pp, '-'))
    feature->value = 0;
  else {
    parse_char (pp, '+');
    feature->value = 1;
  }

  return TRUE;
}

static hb_bool_t
parse_feature_tag (char **pp, hb_feature_t *feature)
{
  char *p = *pp, c;

  parse_space (pp);

#define ISALNUM(c) (('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z') || ('0' <= (c) && (c) <= '9'))
  while (c = **pp, ISALNUM(c))
    (*pp)++;
#undef ISALNUM

  if (p == *pp)
    return FALSE;

  feature->tag = hb_tag_from_string (p, *pp - p);
  return TRUE;
}

static hb_bool_t
parse_feature_indices (char **pp, hb_feature_t *feature)
{
  hb_bool_t has_start;

  feature->start = 0;
  feature->end = (unsigned int) -1;

  if (!parse_char (pp, '['))
    return TRUE;

  has_start = parse_uint (pp, &feature->start);

  if (parse_char (pp, ':')) {
    parse_uint (pp, &feature->end);
  } else {
    if (has_start)
      feature->end = feature->start + 1;
  }

  return parse_char (pp, ']');
}

static hb_bool_t
parse_feature_value_postfix (char **pp, hb_feature_t *feature)
{
  return !parse_char (pp, '=') || parse_uint (pp, &feature->value);
}


static hb_bool_t
parse_one_feature (char **pp, hb_feature_t *feature)
{
  return parse_feature_value_prefix (pp, feature) &&
	 parse_feature_tag (pp, feature) &&
	 parse_feature_indices (pp, feature) &&
	 parse_feature_value_postfix (pp, feature) &&
	 (parse_char (pp, ',') || **pp == '\0');
}

static void
skip_one_feature (char **pp)
{
  char *e;
  e = strchr (*pp, ',');
  if (e)
    *pp = e + 1;
  else
    *pp = *pp + strlen (*pp);
}

static gboolean
parse_features (const char *name G_GNUC_UNUSED,
	        const char *arg,
	        gpointer    data,
	        GError    **error G_GNUC_UNUSED)
{
  shape_options_t *shape_opts = (shape_options_t *) data;
  char *s = (char *) arg;
  char *p;

  shape_opts->num_features = 0;
  shape_opts->features = NULL;

  if (!*s)
    return TRUE;

  /* count the features first, so we can allocate memory */
  p = s;
  do {
    shape_opts->num_features++;
    p = strchr (p, ',');
    if (p)
      p++;
  } while (p);

  shape_opts->features = (hb_feature_t *) calloc (shape_opts->num_features, sizeof (*shape_opts->features));

  /* now do the actual parsing */
  p = s;
  shape_opts->num_features = 0;
  while (*p) {
    if (parse_one_feature (&p, &shape_opts->features[shape_opts->num_features]))
      shape_opts->num_features++;
    else
      skip_one_feature (&p);
  }

  return TRUE;
}


void
view_options_t::add_options (option_parser_t *parser)
{
  GOptionEntry entries[] =
  {
    {"annotate",	0, 0, G_OPTION_ARG_NONE,	&this->annotate,		"Annotate output rendering",				NULL},
    {"background",	0, 0, G_OPTION_ARG_STRING,	&this->back,			"Set background color (default: "DEFAULT_BACK")",	"red/#rrggbb/#rrggbbaa"},
    {"foreground",	0, 0, G_OPTION_ARG_STRING,	&this->fore,			"Set foreground color (default: "DEFAULT_FORE")",	"red/#rrggbb/#rrggbbaa"},
    {"line-space",	0, 0, G_OPTION_ARG_DOUBLE,	&this->line_space,		"Set space between lines (default: 0)",			"units"},
    {"margin",		0, 0, G_OPTION_ARG_CALLBACK,	(gpointer) &parse_margin,	"Margin around output (default: "G_STRINGIFY(DEFAULT_MARGIN)")","one to four numbers"},
    {NULL}
  };
  parser->add_group (entries,
		     "view",
		     "View options:",
		     "Options controlling the output rendering",
		     this);
}

void
shape_options_t::add_options (option_parser_t *parser)
{
  GOptionEntry entries[] =
  {
    {"shapers",		0, 0, G_OPTION_ARG_CALLBACK,	(gpointer) &parse_shapers,	"Comma-separated list of shapers",	"list"},
    {"direction",	0, 0, G_OPTION_ARG_STRING,	&this->direction,		"Set text direction (default: auto)",	"ltr/rtl/ttb/btt"},
    {"language",	0, 0, G_OPTION_ARG_STRING,	&this->language,		"Set text language (default: $LANG)",	"langstr"},
    {"script",		0, 0, G_OPTION_ARG_STRING,	&this->script,			"Set text script (default: auto)",	"ISO-15924 tag"},
    {"features",	0, 0, G_OPTION_ARG_CALLBACK,	(gpointer) &parse_features,	"Font features to apply to text",	"TODO"},
    {NULL}
  };
  parser->add_group (entries,
		     "shape",
		     "Shape options:",
		     "Options controlling the shaping process",
		     this);
}

void
font_options_t::add_options (option_parser_t *parser)
{
  GOptionEntry entries[] =
  {
    {"font-file",	0, 0, G_OPTION_ARG_STRING,	&this->font_file,		"Font file-name",					"filename"},
    {"face-index",	0, 0, G_OPTION_ARG_INT,		&this->face_index,		"Face index (default: 0)",                              "index"},
    {"font-size",	0, 0, G_OPTION_ARG_DOUBLE,	&this->font_size,		"Font size (default: "G_STRINGIFY(DEFAULT_FONT_SIZE)")","size"},
    {NULL}
  };
  parser->add_group (entries,
		     "font",
		     "Font options:",
		     "Options controlling the font",
		     this);
}

void
text_options_t::add_options (option_parser_t *parser)
{
  GOptionEntry entries[] =
  {
    {"text",		0, 0, G_OPTION_ARG_STRING,	&this->text,			"Set input text",			"string"},
    {"text-file",	0, 0, G_OPTION_ARG_STRING,	&this->text_file,		"Set input text file-name",		"filename"},
    {NULL}
  };
  parser->add_group (entries,
		     "text",
		     "Text options:",
		     "Options controlling the input text",
		     this);
}

void
output_options_t::add_options (option_parser_t *parser)
{
  GOptionEntry entries[] =
  {
    {"output",		0, 0, G_OPTION_ARG_STRING,	&this->output_file,		"Set output file-name (default: stdout)","filename"},
    {"format",		0, 0, G_OPTION_ARG_STRING,	&this->output_format,		"Set output format",			"format"},
    {NULL}
  };
  parser->add_group (entries,
		     "output",
		     "Output options:",
		     "Options controlling the output",
		     this);
}



hb_font_t *
font_options_t::get_font (void) const
{
  if (font)
    return font;

  hb_blob_t *blob = NULL;

  /* Create the blob */
  {
    const char *font_data;
    unsigned int len;
    hb_destroy_func_t destroy;
    void *user_data;
    hb_memory_mode_t mm;

    if (!font_file)
      fail (TRUE, "No font file set");

    GMappedFile *mf = g_mapped_file_new (font_file, FALSE, NULL);
    if (!mf)
      fail (FALSE, "Failed opening font file `%s'", font_file);
    font_data = g_mapped_file_get_contents (mf);
    len = g_mapped_file_get_length (mf);
    destroy = (hb_destroy_func_t) g_mapped_file_unref;
    user_data = (void *) mf;
    mm = HB_MEMORY_MODE_READONLY_MAY_MAKE_WRITABLE;

    blob = hb_blob_create (font_data, len, mm, user_data, destroy);
  }

  /* Create the face */
  hb_face_t *face = hb_face_create (blob, face_index);
  hb_blob_destroy (blob);


  font = hb_font_create (face);

  unsigned int upem = hb_face_get_upem (face);
  hb_font_set_scale (font, font_size * upem, font_size * upem);
  hb_face_destroy (face);

#if HAVE_FREETYPE
  hb_ft_font_set_funcs (font);
#endif

  return font;
}


const char *
text_options_t::get_line (unsigned int *len)
{
  if (!text) {
    if (!text_file)
      fail (TRUE, "At least one of text or text-file must be set");

    GMappedFile *mf = g_mapped_file_new (text_file, FALSE, NULL);
    if (!mf)
      fail (FALSE, "Failed opening text file `%s'", text_file);
    text = g_mapped_file_get_contents (mf);
    text_len = g_mapped_file_get_length (mf);
  }

  if (text_len == (unsigned int) -1)
    text_len = strlen (text);

  if (!text_len) {
    *len = 0;
    return NULL;
  }

  const char *ret = text;
  const char *p = (const char *) memchr (text, '\n', text_len);
  unsigned int ret_len;
  if (!p) {
    ret_len = text_len;
    text += ret_len;
    text_len = 0;
  } else {
    ret_len = p - ret;
    text += ret_len + 1;
    text_len -= ret_len + 1;
  }

  *len = ret_len;
  return ret;
}
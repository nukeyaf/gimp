/* LIBGIMP - The GIMP Library
 * Copyright (C) 1995-1997 Spencer Kimball and Peter Mattis
 *
 * Config file serialization and deserialization interface
 * Copyright (C) 2001-2002  Sven Neumann <sven@gimp.org>
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <gio/gio.h>

#include "libgimpbase/gimpbase.h"

#include "gimpconfigtypes.h"

#include "gimpconfigwriter.h"
#include "gimpconfig-iface.h"
#include "gimpconfig-deserialize.h"
#include "gimpconfig-serialize.h"
#include "gimpconfig-params.h"
#include "gimpconfig-utils.h"
#include "gimpscanner.h"

#include "libgimp/libgimp-intl.h"


/**
 * SECTION: gimpconfig-iface
 * @title: GimpConfig-iface
 * @short_description: High-level API for libgimpconfig.
 *
 * High-level API for libgimpconfig.
 **/


/*
 * The GimpConfig serialization and deserialization interface.
 */

static void         gimp_config_iface_base_init (GimpConfigInterface  *config_iface);

static gboolean     gimp_config_iface_serialize   (GimpConfig       *config,
                                                   GimpConfigWriter *writer,
                                                   gpointer          data);
static gboolean     gimp_config_iface_deserialize (GimpConfig       *config,
                                                   GScanner         *scanner,
                                                   gint              nest_level,
                                                   gpointer          data);
static GimpConfig * gimp_config_iface_duplicate   (GimpConfig       *config);
static gboolean     gimp_config_iface_equal       (GimpConfig       *a,
                                                   GimpConfig       *b);
static void         gimp_config_iface_reset       (GimpConfig       *config);
static gboolean     gimp_config_iface_copy        (GimpConfig       *src,
                                                   GimpConfig       *dest,
                                                   GParamFlags       flags);


GType
gimp_config_interface_get_type (void)
{
  static GType config_iface_type = 0;

  if (! config_iface_type)
    {
      const GTypeInfo config_iface_info =
      {
        sizeof (GimpConfigInterface),
        (GBaseInitFunc)     gimp_config_iface_base_init,
        (GBaseFinalizeFunc) NULL,
      };

      config_iface_type = g_type_register_static (G_TYPE_INTERFACE,
                                                  "GimpConfigInterface",
                                                  &config_iface_info,
                                                  0);

      g_type_interface_add_prerequisite (config_iface_type, G_TYPE_OBJECT);
    }

  return config_iface_type;
}

static void
gimp_config_iface_base_init (GimpConfigInterface *config_iface)
{
  if (! config_iface->serialize)
    {
      config_iface->serialize   = gimp_config_iface_serialize;
      config_iface->deserialize = gimp_config_iface_deserialize;
      config_iface->duplicate   = gimp_config_iface_duplicate;
      config_iface->equal       = gimp_config_iface_equal;
      config_iface->reset       = gimp_config_iface_reset;
      config_iface->copy        = gimp_config_iface_copy;
    }

  /*  always set these to NULL since we don't want to inherit them
   *  from parent classes
   */
  config_iface->serialize_property   = NULL;
  config_iface->deserialize_property = NULL;
}

static gboolean
gimp_config_iface_serialize (GimpConfig       *config,
                             GimpConfigWriter *writer,
                             gpointer          data)
{
  return gimp_config_serialize_properties (config, writer);
}

static gboolean
gimp_config_iface_deserialize (GimpConfig *config,
                               GScanner   *scanner,
                               gint        nest_level,
                               gpointer    data)
{
  return gimp_config_deserialize_properties (config, scanner, nest_level);
}

static GimpConfig *
gimp_config_iface_duplicate (GimpConfig *config)
{
  GObject       *object = G_OBJECT (config);
  GObjectClass  *klass  = G_OBJECT_GET_CLASS (object);
  GParamSpec   **property_specs;
  guint          n_property_specs;
  GParameter    *construct_params   = NULL;
  gint           n_construct_params = 0;
  guint          i;
  GObject       *dup;

  property_specs = g_object_class_list_properties (klass, &n_property_specs);

  construct_params = g_new0 (GParameter, n_property_specs);

  for (i = 0; i < n_property_specs; i++)
    {
      GParamSpec *prop_spec = property_specs[i];

      if ((prop_spec->flags & G_PARAM_READABLE) &&
          (prop_spec->flags & G_PARAM_WRITABLE) &&
          (prop_spec->flags & G_PARAM_CONSTRUCT_ONLY))
        {
          GParameter *construct_param;

          construct_param = &construct_params[n_construct_params++];

          construct_param->name = prop_spec->name;

          g_value_init (&construct_param->value, prop_spec->value_type);
          g_object_get_property (object,
                                 prop_spec->name, &construct_param->value);
        }
    }

  g_free (property_specs);

  dup = g_object_newv (G_TYPE_FROM_INSTANCE (object),
                       n_construct_params, construct_params);

  for (i = 0; i < n_construct_params; i++)
    g_value_unset (&construct_params[i].value);

  g_free (construct_params);

  gimp_config_copy (config, GIMP_CONFIG (dup), 0);

  return GIMP_CONFIG (dup);
}

static gboolean
gimp_config_iface_equal (GimpConfig *a,
                         GimpConfig *b)
{
  GObjectClass  *klass;
  GParamSpec   **property_specs;
  guint          n_property_specs;
  guint          i;
  gboolean       equal = TRUE;

  klass = G_OBJECT_GET_CLASS (a);

  property_specs = g_object_class_list_properties (klass, &n_property_specs);

  for (i = 0; equal && i < n_property_specs; i++)
    {
      GParamSpec  *prop_spec;
      GValue       a_value = { 0, };
      GValue       b_value = { 0, };

      prop_spec = property_specs[i];

      if (! (prop_spec->flags & G_PARAM_READABLE))
        continue;

      g_value_init (&a_value, prop_spec->value_type);
      g_value_init (&b_value, prop_spec->value_type);
      g_object_get_property (G_OBJECT (a), prop_spec->name, &a_value);
      g_object_get_property (G_OBJECT (b), prop_spec->name, &b_value);

      if (g_param_values_cmp (prop_spec, &a_value, &b_value))
        {
          if ((prop_spec->flags & GIMP_CONFIG_PARAM_AGGREGATE) &&
              G_IS_PARAM_SPEC_OBJECT (prop_spec)        &&
              g_type_interface_peek (g_type_class_peek (prop_spec->value_type),
                                     GIMP_TYPE_CONFIG))
            {
              if (! gimp_config_is_equal_to (g_value_get_object (&a_value),
                                             g_value_get_object (&b_value)))
                {
                  equal = FALSE;
                }
            }
          else
            {
              equal = FALSE;
            }
        }

      g_value_unset (&a_value);
      g_value_unset (&b_value);
    }

  g_free (property_specs);

  return equal;
}

static void
gimp_config_iface_reset (GimpConfig *config)
{
  gimp_config_reset_properties (G_OBJECT (config));
}

static gboolean
gimp_config_iface_copy (GimpConfig  *src,
                        GimpConfig  *dest,
                        GParamFlags  flags)
{
  return gimp_config_sync (G_OBJECT (src), G_OBJECT (dest), flags);
}

/**
 * gimp_config_serialize_to_file:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @filename: the name of the file to write the configuration to.
 * @header: optional file header (must be ASCII only)
 * @footer: optional file footer (must be ASCII only)
 * @data: user data passed to the serialize implementation.
 * @error: return location for a possible error
 *
 * Serializes the object properties of @config to the file specified
 * by @filename. If a file with that name already exists, it is
 * overwritten. Basically this function opens @filename for you and
 * calls the serialize function of the @config's #GimpConfigInterface.
 *
 * Return value: %TRUE if serialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_serialize_to_file (GimpConfig   *config,
                               const gchar  *filename,
                               const gchar  *header,
                               const gchar  *footer,
                               gpointer      data,
                               GError      **error)
{
  GimpConfigWriter *writer;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  writer = gimp_config_writer_new_file (filename, TRUE, header, error);
  if (!writer)
    return FALSE;

  GIMP_CONFIG_GET_INTERFACE (config)->serialize (config, writer, data);

  return gimp_config_writer_finish (writer, footer, error);
}

/**
 * gimp_config_serialize_to_gfile:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @file:   the #GFile to write the configuration to.
 * @header: optional file header (must be ASCII only)
 * @footer: optional file footer (must be ASCII only)
 * @data: user data passed to the serialize implementation.
 * @error: return location for a possible error
 *
 * Serializes the object properties of @config to the file specified
 * by @file. If a file with that name already exists, it is
 * overwritten. Basically this function opens @file for you and calls
 * the serialize function of the @config's #GimpConfigInterface.
 *
 * Return value: %TRUE if serialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.10
 **/
gboolean
gimp_config_serialize_to_gfile (GimpConfig   *config,
                                GFile        *file,
                                const gchar  *header,
                                const gchar  *footer,
                                gpointer      data,
                                GError      **error)
{
  GimpConfigWriter *writer;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (G_IS_FILE (file), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  writer = gimp_config_writer_new_gfile (file, TRUE, header, error);
  if (!writer)
    return FALSE;

  GIMP_CONFIG_GET_INTERFACE (config)->serialize (config, writer, data);

  return gimp_config_writer_finish (writer, footer, error);
}

/**
 * gimp_config_serialize_to_stream:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @stream: the #GOutputStream to write the configuration to.
 * @header: optional file header (must be ASCII only)
 * @footer: optional file footer (must be ASCII only)
 * @data: user data passed to the serialize implementation.
 * @error: return location for a possible error
 *
 * Serializes the object properties of @config to the stream specified
 * by @stream.
 *
 * Return value: %TRUE if serialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.10
 **/
gboolean
gimp_config_serialize_to_stream (GimpConfig     *config,
                                 GOutputStream  *output,
                                 const gchar    *header,
                                 const gchar    *footer,
                                 gpointer        data,
                                 GError        **error)
{
  GimpConfigWriter *writer;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (G_IS_OUTPUT_STREAM (output), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  writer = gimp_config_writer_new_stream (output, header, error);
  if (!writer)
    return FALSE;

  GIMP_CONFIG_GET_INTERFACE (config)->serialize (config, writer, data);

  return gimp_config_writer_finish (writer, footer, error);
}

/**
 * gimp_config_serialize_to_fd:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @fd: a file descriptor, opened for writing
 * @data: user data passed to the serialize implementation.
 *
 * Serializes the object properties of @config to the given file
 * descriptor.
 *
 * Return value: %TRUE if serialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_serialize_to_fd (GimpConfig *config,
                             gint        fd,
                             gpointer    data)
{
  GimpConfigWriter *writer;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (fd > 0, FALSE);

  writer = gimp_config_writer_new_fd (fd);
  if (!writer)
    return FALSE;

  GIMP_CONFIG_GET_INTERFACE (config)->serialize (config, writer, data);

  return gimp_config_writer_finish (writer, NULL, NULL);
}

/**
 * gimp_config_serialize_to_string:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @data: user data passed to the serialize implementation.
 *
 * Serializes the object properties of @config to a string.
 *
 * Return value: a newly allocated NUL-terminated string.
 *
 * Since: GIMP 2.4
 **/
gchar *
gimp_config_serialize_to_string (GimpConfig *config,
                                 gpointer    data)
{
  GimpConfigWriter *writer;
  GString          *str;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), NULL);

  str = g_string_new (NULL);
  writer = gimp_config_writer_new_string (str);

  GIMP_CONFIG_GET_INTERFACE (config)->serialize (config, writer, data);

  gimp_config_writer_finish (writer, NULL, NULL);

  return g_string_free (str, FALSE);
}

/**
 * gimp_config_deserialize_file:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @filename: the name of the file to read configuration from.
 * @data: user data passed to the deserialize implementation.
 * @error: return location for a possible error
 *
 * Opens the file specified by @filename, reads configuration data
 * from it and configures @config accordingly. Basically this function
 * creates a properly configured #GScanner for you and calls the
 * deserialize function of the @config's #GimpConfigInterface.
 *
 * Return value: %TRUE if deserialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_deserialize_file (GimpConfig   *config,
                              const gchar  *filename,
                              gpointer      data,
                              GError      **error)
{
  GScanner *scanner;
  gboolean  success;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scanner = gimp_scanner_new_file (filename, error);
  if (! scanner)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (config));

  success = GIMP_CONFIG_GET_INTERFACE (config)->deserialize (config,
                                                             scanner, 0, data);

  g_object_thaw_notify (G_OBJECT (config));

  gimp_scanner_destroy (scanner);

  if (! success)
    g_assert (error == NULL || *error != NULL);

  return success;
}

/**
 * gimp_config_deserialize_gfile:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @file: the #GFile to read configuration from.
 * @data: user data passed to the deserialize implementation.
 * @error: return location for a possible error
 *
 * Opens the file specified by @file, reads configuration data from it
 * and configures @config accordingly. Basically this function creates
 * a properly configured #GScanner for you and calls the deserialize
 * function of the @config's #GimpConfigInterface.
 *
 * Return value: %TRUE if deserialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.10
 **/
gboolean
gimp_config_deserialize_gfile (GimpConfig  *config,
                               GFile       *file,
                               gpointer     data,
                               GError     **error)
{
  GScanner *scanner;
  gboolean  success;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (G_IS_FILE (file), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scanner = gimp_scanner_new_gfile (file, error);
  if (! scanner)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (config));

  success = GIMP_CONFIG_GET_INTERFACE (config)->deserialize (config,
                                                             scanner, 0, data);

  g_object_thaw_notify (G_OBJECT (config));

  gimp_scanner_destroy (scanner);

  if (! success)
    g_assert (error == NULL || *error != NULL);

  return success;
}

/**
 * gimp_config_deserialize_stream:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @input: the #GInputStream to read configuration from.
 * @data: user data passed to the deserialize implementation.
 * @error: return location for a possible error
 *
 * Reads configuration data from @input and configures @config
 * accordingly. Basically this function creates a properly configured
 * #GScanner for you and calls the deserialize function of the
 * @config's #GimpConfigInterface.
 *
 * Return value: %TRUE if deserialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.10
 **/
gboolean
gimp_config_deserialize_stream (GimpConfig    *config,
                                GInputStream  *input,
                                gpointer       data,
                                GError       **error)
{
  GScanner *scanner;
  gboolean  success;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (G_IS_INPUT_STREAM (input), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scanner = gimp_scanner_new_stream (input, error);
  if (! scanner)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (config));

  success = GIMP_CONFIG_GET_INTERFACE (config)->deserialize (config,
                                                             scanner, 0, data);

  g_object_thaw_notify (G_OBJECT (config));

  gimp_scanner_destroy (scanner);

  if (! success)
    g_assert (error == NULL || *error != NULL);

  return success;
}

/**
 * gimp_config_deserialize_string:
 * @config:   a #GObject that implements the #GimpConfigInterface.
 * @text:     string to deserialize (in UTF-8 encoding)
 * @text_len: length of @text in bytes or -1
 * @data:     client data
 * @error:    return location for a possible error
 *
 * Configures @config from @text. Basically this function creates a
 * properly configured #GScanner for you and calls the deserialize
 * function of the @config's #GimpConfigInterface.
 *
 * Returns: %TRUE if deserialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_deserialize_string (GimpConfig      *config,
                                const gchar  *text,
                                gint          text_len,
                                gpointer      data,
                                GError      **error)
{
  GScanner *scanner;
  gboolean  success;

  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);
  g_return_val_if_fail (text != NULL || text_len == 0, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scanner = gimp_scanner_new_string (text, text_len, error);

  g_object_freeze_notify (G_OBJECT (config));

  success = GIMP_CONFIG_GET_INTERFACE (config)->deserialize (config,
                                                             scanner, 0, data);

  g_object_thaw_notify (G_OBJECT (config));

  gimp_scanner_destroy (scanner);

  if (! success)
    g_assert (error == NULL || *error != NULL);

  return success;
}

/**
 * gimp_config_deserialize_return:
 * @scanner:        a #GScanner
 * @expected_token: the expected token
 * @nest_level:     the next level
 *
 * Returns:
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_deserialize_return (GScanner     *scanner,
                                GTokenType    expected_token,
                                gint          nest_level)
{
  GTokenType next_token;

  g_return_val_if_fail (scanner != NULL, FALSE);

  next_token = g_scanner_peek_next_token (scanner);

  if (expected_token != G_TOKEN_LEFT_PAREN)
    {
      g_scanner_get_next_token (scanner);
      g_scanner_unexp_token (scanner, expected_token, NULL, NULL, NULL,
                             _("fatal parse error"), TRUE);
      return FALSE;
    }
  else
    {
      if (nest_level > 0 && next_token == G_TOKEN_RIGHT_PAREN)
        {
          return TRUE;
        }
      else if (next_token != G_TOKEN_EOF)
        {
          g_scanner_get_next_token (scanner);
          g_scanner_unexp_token (scanner, expected_token, NULL, NULL, NULL,
                                 _("fatal parse error"), TRUE);
          return FALSE;
        }
    }

  return TRUE;
}


/**
 * gimp_config_serialize:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @writer: the #GimpConfigWriter to use.
 * @data: client data
 *
 * Serialize the #GimpConfig object.
 *
 * Returns: %TRUE if serialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.8
 **/
gboolean
gimp_config_serialize (GimpConfig       *config,
                       GimpConfigWriter *writer,
                       gpointer          data)
{
  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);

  return GIMP_CONFIG_GET_INTERFACE (config)->serialize (config,
                                                        writer,
                                                        data);
}

/**
 * gimp_config_deserialize:
 * @config: a #GObject that implements the #GimpConfigInterface.
 * @scanner: the #GScanner to use.
 * @nest_level: the nest level.
 * @data: client data.
 *
 * Deserialize the #GimpConfig object.
 *
 * Returns: %TRUE if deserialization succeeded, %FALSE otherwise.
 *
 * Since: GIMP 2.8
 **/
gboolean
gimp_config_deserialize (GimpConfig *config,
                         GScanner   *scanner,
                         gint        nest_level,
                         gpointer    data)
{
  g_return_val_if_fail (GIMP_IS_CONFIG (config), FALSE);

  return GIMP_CONFIG_GET_INTERFACE (config)->deserialize (config,
                                                          scanner,
                                                          nest_level,
                                                          data);
}

/**
 * gimp_config_duplicate:
 * @config: a #GObject that implements the #GimpConfigInterface.
 *
 * Creates a copy of the passed object by copying all object
 * properties. The default implementation of the #GimpConfigInterface
 * only works for objects that are completely defined by their
 * properties.
 *
 * Return value: the duplicated #GimpConfig object
 *
 * Since: GIMP 2.4
 **/
gpointer
gimp_config_duplicate (GimpConfig *config)
{
  g_return_val_if_fail (GIMP_IS_CONFIG (config), NULL);

  return GIMP_CONFIG_GET_INTERFACE (config)->duplicate (config);
}

/**
 * gimp_config_is_equal_to:
 * @a: a #GObject that implements the #GimpConfigInterface.
 * @b: another #GObject of the same type as @a.
 *
 * Compares the two objects. The default implementation of the
 * #GimpConfigInterface compares the object properties and thus only
 * works for objects that are completely defined by their
 * properties.
 *
 * Return value: %TRUE if the two objects are equal.
 *
 * Since: GIMP 2.4
 **/
gboolean
gimp_config_is_equal_to (GimpConfig *a,
                         GimpConfig *b)
{
  g_return_val_if_fail (GIMP_IS_CONFIG (a), FALSE);
  g_return_val_if_fail (GIMP_IS_CONFIG (b), FALSE);
  g_return_val_if_fail (G_TYPE_FROM_INSTANCE (a) == G_TYPE_FROM_INSTANCE (b),
                        FALSE);

  return GIMP_CONFIG_GET_INTERFACE (a)->equal (a, b);
}

/**
 * gimp_config_reset:
 * @config: a #GObject that implements the #GimpConfigInterface.
 *
 * Resets the object to its default state. The default implementation of the
 * #GimpConfigInterface only works for objects that are completely defined by
 * their properties.
 *
 * Since: GIMP 2.4
 **/
void
gimp_config_reset (GimpConfig *config)
{
  g_return_if_fail (GIMP_IS_CONFIG (config));

  g_object_freeze_notify (G_OBJECT (config));

  GIMP_CONFIG_GET_INTERFACE (config)->reset (config);

  g_object_thaw_notify (G_OBJECT (config));
}

/**
 * gimp_config_copy:
 * @src: a #GObject that implements the #GimpConfigInterface.
 * @dest: another #GObject of the same type as @a.
 * @flags: a mask of GParamFlags
 *
 * Compares all read- and write-able properties from @src and @dest
 * that have all @flags set. Differing values are then copied from
 * @src to @dest. If @flags is 0, all differing read/write properties.
 *
 * Properties marked as "construct-only" are not touched.
 *
 * Return value: %TRUE if @dest was modified, %FALSE otherwise
 *
 * Since: GIMP 2.6
 **/
gboolean
gimp_config_copy (GimpConfig  *src,
                  GimpConfig  *dest,
                  GParamFlags  flags)
{
  gboolean changed;

  g_return_val_if_fail (GIMP_IS_CONFIG (src), FALSE);
  g_return_val_if_fail (GIMP_IS_CONFIG (dest), FALSE);
  g_return_val_if_fail (G_TYPE_FROM_INSTANCE (src) == G_TYPE_FROM_INSTANCE (dest),
                        FALSE);

  g_object_freeze_notify (G_OBJECT (dest));

  changed = GIMP_CONFIG_GET_INTERFACE (src)->copy (src, dest, flags);

  g_object_thaw_notify (G_OBJECT (dest));

  return changed;
}

/* Text-include before g_hash_table_destroy in pinned glib_compat.c. Final-owner
 * destruction must free directly rather than first resizing an empty table. */
static gboolean qa_unicorn_hash_destroy_single_owner(GHashTable *table)
{
    if (table->ref_count != 1) return FALSE;
    g_hash_table_unref(table);
    return TRUE;
}

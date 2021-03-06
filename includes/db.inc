<?php

/**
 * @file
 * This file contains DB query functions.
 */

use Drupal\Core\Url;

/**
 * Get the configuration of a Solr field.
 */
function islandora_solr_get_field_configuration($field_type, $field_name) {
  $query = \Drupal::database()->select('islandora_solr_fields', 'isf');
  $query->fields('isf', ['solr_field_settings'])
    ->condition('field_type', $field_type)
    ->condition('solr_field', $field_name);
  $result = $query->execute();
  $records = $result->fetchAll(PDO::FETCH_ASSOC);
  return unserialize(reset($records)['solr_field_settings']);
}

/**
 * Set the configuration of a Solr field.
 */
function islandora_solr_set_field_configuration($field_type, $field_name, $settings) {
  $query = \Drupal::database()->update('islandora_solr_fields');
  $query->fields(['solr_field_settings' => serialize($settings)]);
  $query->condition('field_type', $field_type)
    ->condition('solr_field', $field_name)
    ->execute();
}

/**
 * Performs a select query to get fields for any field type.
 *
 * @params string $field_type
 *   The type of field to query for. If not passed, the query returns all
 *   fields.
 * @params bool $filter
 *   If set to TRUE, it will filter the array against user roles. If set to
 *   FALSE, it will return all results.
 * @params bool $simplify
 *   Whether to return the fields array as a simple term => label array.
 *
 * @return array
 *   The query results as an array.
 *
 * @see islandora_solr_get_fields()
 */
function islandora_solr_get_fields($field_type = NULL, $filter = TRUE, $simplified = TRUE, $keys = FALSE) {
  // Fields query.
  $query = db_select('islandora_solr_fields', 'isf');
  $query->fields('isf');
  if ($field_type !== NULL) {
    $query->condition('isf.field_type', $field_type, '=');
  }
  $query->orderBy('isf.weight', 'ASC');
  try {
    $result = $query->execute();
    $records = $result->fetchAll(PDO::FETCH_ASSOC);
  }
  catch (Exception $e) {
    if ($e->errorInfo[0] == '42S02') {
      drupal_set_message(t('Islandora Solr fields table not found. Try running <a href="@update_url">update.php</a>.', ['@update_url' => Url::fromRoute('system.db_update')]), 'error');
    }
    else {
      drupal_set_message($e->getMessage(), 'error');
    }
    $records = [];
  }
  // Unserialize Solr field settings blob.
  foreach ($records as &$value) {
    $value['solr_field_settings'] = unserialize($value['solr_field_settings']);
  }
  // Filter.
  if ($filter == TRUE) {
    $records = _islandora_solr_filter_fields($records);
  }

  // Simplify.
  if ($simplified == TRUE) {
    $records = _islandora_solr_simplify_fields($records);
  }
  // Fields as keys.
  if ($keys == TRUE && $simplified == FALSE) {
    $original = $records;
    $records = [];
    foreach ($original as $field) {
      $records[$field['solr_field']] = $field;
    }
  }
  return $records;
}

/**
 * Checks an array of fields DB records against user roles.
 *
 * @param array $records
 *   An array of fields.
 *
 * @return array
 *   The array of fields, filtered by user roles.
 *
 * @see islandora_solr_get_fields()
 */
function _islandora_solr_filter_fields(array $records = []) {
  $user = \Drupal::currentUser();
  // No checks for user 1.
  if ($user->id() == 1) {
    return $records;
  }
  $filtered_records = [];
  foreach ($records as $record) {
    // If field settings are set, check permissions.
    if (isset($record['solr_field_settings']) && !empty($record['solr_field_settings']) && $record['solr_field_settings']['permissions']) {
      // Get field permissions.
      $field_roles = $record['solr_field_settings']['permissions'];
      // Check field permissions against user roles.
      $check = array_intersect($field_roles, $user->getRoles());
      if (!empty($check)) {
        $filtered_records[] = $record;
      }
    }
    else {
      // If permissions are never explicitly set, include by default.
      $filtered_records[] = $record;
    }
  }
  return $filtered_records;
}

/**
 * Formats fields data from the DB as a clean array.
 *
 * @param array $fields
 *   Raw array from the DB with fields data.
 *
 * @return array
 *   Returns a cleaned up array as term => label
 *
 * @see islandora_solr_get_fields()
 */
function _islandora_solr_simplify_fields(array $fields = []) {
  // Render as term => label.
  $records = [];
  foreach ($fields as $value) {
    $term = $value['solr_field'];
    if (isset($value['solr_field_settings']['label']) && !empty($value['solr_field_settings']['label'])) {
      $label = $value['solr_field_settings']['label'];
    }
    else {
      $label = $term;
    }
    $records[$term] = $label;
  }
  return $records;
}

/**
 * Return display fields with snippet highlighting enabled.
 */
function islandora_solr_get_snippet_fields() {
  $records = islandora_solr_get_fields('result_fields', TRUE, FALSE);
  $snippet_fields = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['snippet']) && $value['solr_field_settings']['snippet'] == 1) {
      $snippet_fields[] = $value['solr_field'];
    }
  }
  return $snippet_fields;
}

/**
 * Return display fields with 'link to object' enabled.
 */
function islandora_solr_get_link_to_object_fields() {
  $records = islandora_solr_get_fields('result_fields', TRUE, FALSE);
  $link_to_object_fields = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['link_rendering']) && $value['solr_field_settings']['link_rendering'] == 'object') {
      $link_to_object_fields[] = $value['solr_field'];
    }
  }
  return $link_to_object_fields;
}

/**
 * Return display fields that have a length limit set.
 *
 * Returns an associative array with field names as key and truncation
 * character to use (if any) as value.
 */
function islandora_solr_get_truncate_length_fields() {
  $records = islandora_solr_get_fields('result_fields', TRUE, FALSE);
  $truncate_length_fields = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['maximum_length']) && intval($value['solr_field_settings']['maximum_length']) > 0) {
      $truncate_length_fields[$value['solr_field']] = [
        'maximum_length' => $value['solr_field_settings']['maximum_length'],
      ];
      if (isset($value['solr_field_settings']['add_ellipsis'])) {
        $truncate_length_fields[$value['solr_field']]['add_ellipsis'] = $value['solr_field_settings']['add_ellipsis'];
      }
      if (isset($value['solr_field_settings']['wordsafe'])) {
        $truncate_length_fields[$value['solr_field']]['wordsafe'] = $value['solr_field_settings']['wordsafe'];
        $truncate_length_fields[$value['solr_field']]['wordsafe_length'] = $value['solr_field_settings']['wordsafe_length'];
      }
      if (isset($value['solr_field_settings']['truncation_type'])) {
        $truncate_length_fields[$value['solr_field']]['truncation_type'] = $value['solr_field_settings']['truncation_type'];
      }
    }
  }
  return $truncate_length_fields;
}

/**
 * Return display fields with 'link to search' enabled.
 */
function islandora_solr_get_link_to_search_fields() {
  $records = islandora_solr_get_fields('result_fields', TRUE, FALSE);
  $link_to_search_fields = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['link_rendering']) && $value['solr_field_settings']['link_rendering'] == 'search') {
      $link_to_search_fields[] = $value['solr_field'];
    }
  }
  return $link_to_search_fields;
}

/**
 * Return display fields with 'date format' enabled.
 */
function islandora_solr_get_date_format_fields() {
  $records = islandora_solr_get_fields('result_fields', TRUE, FALSE);
  $date_format = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['date_format']) && !empty($value['solr_field_settings']['date_format'])) {
      $date_format[$value['solr_field']] = $value['solr_field_settings']['date_format'];
    }
  }
  return $date_format;
}

/**
 * Return facet fields with range enabled.
 */
function islandora_solr_get_range_facets() {
  $records = islandora_solr_get_fields('facet_fields', TRUE, FALSE);
  $range_facets = [];
  foreach ($records as $value) {
    if (isset($value['solr_field_settings']['range_facet_select']) && $value['solr_field_settings']['range_facet_select'] == 1) {
      $range_facets[] = $value;
    }
  }
  return $range_facets;
}

/**
 * Return non-range facets with date formatting enabled.
 */
function islandora_solr_get_date_format_facets() {
  $records = islandora_solr_get_fields('facet_fields', TRUE, FALSE);
  $date_format = [];
  foreach ($records as $values) {
    if (!isset($values['solr_field_settings']['range_facet_select']) && isset($values['solr_field_settings']['date_facet_format'])) {
      $date_format[$values['solr_field']] = $values;
    }
  }
  return $date_format;
}

/**
 * Get the sort string for a collection.
 *
 * @param string $pid
 *   The PID of the collection.
 * @param bool $use_fallbacks
 *   Whether or not to attempt to use collection sort fallbacks.
 *
 * @return string|null
 *   The sort string for this collection, or NULL if none exist.
 */
function islandora_solr_get_collection_sort_string($pid, $use_fallbacks = FALSE) {
  // Try to get the collection sort entry for this collection, if enabled.
  if (\Drupal::config('islandora_solr.settings')->get('islandora_solr_individual_collection_sorting')) {
    $query = db_select('islandora_solr_collection_sort_strings', 'ss')
      ->fields('ss', ['sort_string'])
      ->condition('collection_pid', $pid, '=')
      ->execute()
      ->fetchField();

    if (is_string($query)) {
      return $query;
    }
  }

  // Fall back, if wanted.
  if ($use_fallbacks) {
    $sort = \Drupal::config('islandora_solr.settings')->get('islandora_solr_collection_sort');
    if (!empty($sort)) {
      return $sort;
    }
    $sort = \Drupal::config('islandora_solr.settings')->get('islandora_solr_base_sort');
    if (!empty($sort)) {
      return $sort;
    }
  }
  // If all else fails, just don't set it at all.
  return NULL;
}

/**
 * Sets or updates the Solr sort string for a collection.
 *
 * @param string $pid
 *   The collection PID.
 * @param string $string
 *   The string to set it to.
 */
function islandora_solr_set_collection_sort_string($pid, $string) {
  $current = islandora_solr_get_collection_sort_string($pid);
  $message_args = [
    '@collection' => $pid,
    '@sort_string' => $string,
  ];
  // If the current sort string is NULL, then the PID wasn't found in the
  // database.
  if ($current !== $string) {
    if (!empty($string)) {
      db_merge('islandora_solr_collection_sort_strings')
        ->key(['collection_pid' => $pid])
        ->fields([
          'collection_pid' => $pid,
          'sort_string' => $string,
        ])
        ->execute();
      drupal_set_message(t('Sort string for @collection set to "@sort_string".', $message_args));
    }
    // In the case where the new string is empty, drop this entry so we can fall
    // back to global defaults.
    else {
      db_delete('islandora_solr_collection_sort_strings')
        ->condition('collection_pid', $pid)
        ->execute();
      drupal_set_message(t('Removed sort string for collection @collection.', $message_args));
    }
  }
  else {
    drupal_set_message(t('No change was made to the sort string for collection @collection.', $message_args));
  }
}

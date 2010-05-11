<?php
// $Id$

/**
 * @file
 * Database schema code for Microsoft SQL Server database servers.
 */

/**
 * @ingroup schemaapi
 * @{
 */

class DatabaseSchema_sqlsrv extends DatabaseSchema {

  /**
   * Expand a Drupal table name to database, schema and table.
   */
  protected function expandTableName($table_name) {
    $info = $this->connection->getConnectionOptions();

    // The table name may describe the schema eg. schema.table.
    if (strpos($table_name, '.')) {
      list($schema, $table_name) = explode('.', $table_name);
    }
    else {
      $schema = 'dbo';
    }

    return array(
      'database' => $info['database'],
      'schema' => $schema,
      'table' => $table_name,
    );
  }

  protected function buildTableNameCondition($table_name, $operator = '=') {
    $table_info = $this->expandTableName($table_name);

    $condition = new DatabaseCondition('AND');
    $condition->condition('table_catalog', $table_info['database']);
    $condition->condition('table_schema', $table_info['schema']);
    $condition->condition('table_name', $table_info['table'], $operator);
    return $condition;
  }

  protected $additionalColumnInformation = NULL;

  /**
   * Database introspection: fetch technical information about a table.
   */
  public function queryColumnInformation($table) {
    if (!isset($this->additionalColumnInformation)) {
      $this->additionalColumnInformation = array();

      // Don't use {} around information_schema.columns table.
      $result = $this->connection->query("SELECT table_schema, table_name, column_name FROM information_schema.columns WHERE data_type = :type", array(':type' => 'varbinary'));
      foreach ($result as $column) {
        $this->additionalColumnInformation[$column->table_schema . '.' . $column->table_name]['blobs'][$column->column_name] = TRUE;
      }

      // Don't use {} around system tables.
      $result = $this->connection->query('SELECT s.name AS schema_name, t.name AS table_name, ic.name AS column_name FROM sys.identity_columns ic LEFT JOIN sys.tables t ON t.object_id = ic.object_id LEFT JOIN sys.schemas s ON s.schema_id = t.schema_id');
      foreach ($result as $column) {
        $this->additionalColumnInformation[$column->schema_name . '.' . $column->table_name]['identities'][$column->column_name] = TRUE;
      }
    }

    // Generate a key to reference this table's information on.
    $key = $this->connection->prefixTables('{' . $table . '}');
    $key = preg_replace('/[\[\]]/', '', $key);
    if (!strpos($key, '.')) {
      $key = 'dbo.' . $key;
    }

    return isset($this->additionalColumnInformation[$key]) ? $this->additionalColumnInformation[$key] : array();
  }

  // TODO: implement the same for alter table and add/remove fields.
  public function createTable($name, $table) {
    parent::createTable($name, $table);
    $this->additionalColumnInformation = NULL;
  }

  public function tableExists($table) {
    try {
      $this->connection->query('SELECT TOP(1) 1 FROM {' . $table . '}');
      return TRUE;
    }
    catch (PDOException $e) {
      return FALSE;
    }
  }

  /**
   * Generate SQL to create a new table from a Drupal schema definition.
   *
   * @param $name
   *   The name of the table to create.
   * @param $table
   *   A Schema API table definition array.
   * @return
   *   An array of SQL statements to create the table.
   */
  protected function createTableSql($name, $table) {
    $sql_fields = array();
    foreach ($table['fields'] as $field_name => $field) {
      $sql_fields[] = $this->createFieldSql($name, $field_name, $this->processField($field));
    }

    $sql_keys = array();
    if (isset($table['primary key']) && is_array($table['primary key'])) {
      $sql_fields[] = 'CONSTRAINT {' . $name . '}_pkey PRIMARY KEY NONCLUSTERED (' . implode(', ', $this->connection->quoteIdentifiers($table['primary key'])) . ')';
    }

    $sql = "CREATE TABLE [{" . $name . "}] (\n\t";
    $sql .= implode(",\n\t", $sql_fields);
    $sql .= "\n)";
    $statements[] = $sql;

    if (isset($table['unique keys']) && is_array($table['unique keys'])) {
      foreach ($table['unique keys'] as $key_name => $key) {
        $statements[] = $this->addUniqueKeySql($name, $key_name, $key);
      }
    }

    if (isset($table['indexes']) && is_array($table['indexes'])) {
      foreach ($table['indexes'] as $key_name => $key) {
        $statements[] = $this->createIndexSql($name, $key_name, $key);
      }
    }

    return $statements;
  }

  /**
   * Create an SQL string for a field to be used in table creation or
   * alteration.
   *
   * Before passing a field out of a schema definition into this
   * function it has to be processed by _db_process_field().
   *
   * @param $table
   *    The name of the table.
   * @param $name
   *    Name of the field.
   * @param $spec
   *    The field specification, as per the schema data structure format.
   */
  protected function createFieldSql($table, $name, $spec, $skip_checks = FALSE) {
    $sql = $this->connection->quoteIdentifier($name) . ' ' . $spec['sqlsrv_type'];

    if (!empty($spec['length'])) {
      $sql .= '(' . $spec['length'] . ')';
    }
    elseif (isset($spec['precision']) && isset($spec['scale'])) {
      $sql .= '(' . $spec['precision'] . ', ' . $spec['scale'] . ')';
    }

    if (isset($spec['not null']) && $spec['not null']) {
      $sql .= ' NOT NULL';
    }

    if (!$skip_checks) {
      if (isset($spec['default'])) {
        $default = is_string($spec['default']) ? "'" . addslashes($spec['default']) . "'" : $spec['default'];
        $sql .= ' CONSTRAINT {' . $table . '}_' . $name . '_df DEFAULT ' . $default;
      }
      if (!empty($spec['identity'])) {
        $sql .= ' IDENTITY';
      }
      if (!empty($spec['unsigned'])) {
        $sql .= ' CHECK (' . $name . ' >= 0)';
      }
    }
    return $sql;
  }

  /**
   * Return a list of columns for an index definition.
   */
  protected function createKeySql($fields) {
    $ret = array();
    foreach ($fields as $field) {
      if (is_array($field)) {
        $ret[] = $field[0];
      }
      else {
        $ret[] = $field;
      }
    }
    return implode(', ', $ret);
  }

  /**
   * Return the SQL Statement to create an index.
   */
  protected function createIndexSql($table, $name, $fields) {
    // Here we need to create a computed PERSISTENT column, and index that, when
    // the type is not allowed in an index.
    return 'CREATE INDEX ' . $name . '_idx ON [{' . $table . '}] (' . $this->createKeySql($fields) . ')';
  }

  /**
   * Set database-engine specific properties for a field.
   *
   * @param $field
   *   A field description array, as specified in the schema documentation.
   */
  protected function processField($field) {
    if (!isset($field['size'])) {
      $field['size'] = 'normal';
    }
    // Set the correct database-engine specific datatype.
    if (!isset($field['sqlsrv_type'])) {
      $map = $this->getFieldTypeMap();
      $field['sqlsrv_type'] = $map[$field['type'] . ':' . $field['size']];
    }
    if ($field['type'] == 'serial') {
      $field['identity'] = TRUE;
    }
    return $field;
  }

  /**
   * This maps a generic data type in combination with its data size
   * to the engine-specific data type.
   */
  function getFieldTypeMap() {
    // Put :normal last so it gets preserved by array_flip.  This makes
    // it much easier for modules (such as schema.module) to map
    // database types back into schema types.
    return array(
      'varchar:normal' => 'nvarchar',
      'char:normal' => 'nchar',

      'text:tiny' => 'nvarchar(max)',
      'text:small' => 'nvarchar(max)',
      'text:medium' => 'nvarchar(max)',
      'text:big' => 'nvarchar(max)',
      'text:normal' => 'nvarchar(max)',

      'serial:tiny'     => 'smallint',
      'serial:small'    => 'smallint',
      'serial:medium'   => 'int',
      'serial:big'      => 'bigint',
      'serial:normal'   => 'int',

      'int:tiny' => 'smallint',
      'int:small' => 'smallint',
      'int:medium' => 'int',
      'int:big' => 'bigint',
      'int:normal' => 'int',

      'float:tiny' => 'real',
      'float:small' => 'real',
      'float:medium' => 'real',
      'float:big' => 'float(53)',
      'float:normal' => 'real',

      'numeric:normal' => 'numeric',

      'blob:big' => 'varbinary(max)',
      'blob:normal' => 'varbinary(max)',

      'datetime:normal' => 'timestamp',
      'date:normal'     => 'date',
      'datetime:normal' => 'datetime2(0)',
      'time:normal'     => 'time(0)',
    );
  }

  /**
   * Override DatabaseSchema::renameTable().
   *
   * @status complete
   */
  public function renameTable($table, $new_name) {
    if (!$this->tableExists($table)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot rename %table to %table_new: table %table doesn't exist.", array('%table' => $table, '%table_new' => $new_name)));
    }
    if ($this->tableExists($new_name)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot rename %table to %table_new: table %table_new already exists.", array('%table' => $table, '%table_new' => $new_name)));
    }

    $old_table_info = $this->expandTableName($this->connection->prefixTables('{' . $table . '}'));
    $new_table_info = $this->expandTableName($this->connection->prefixTables('{' . $new_name . '}'));

    $this->connection->query('EXEC sp_rename :old, :new', array(
      ':old' => $old_table_info['schema'] . '.' . $old_table_info['table'],
      ':new' => $new_table_info['table'],
    ));

    // Constraint names are global in SQL Server, so we need to rename them
    // when renaming the table. For some strange reason, indexes are local to
    // a table.
    $condition = $this->buildTableNameCondition($this->connection->prefixTables('{' . $new_name . '}'));
    $condition->compile($this->connection, $this);
    $columns = $this->connection->query('SELECT column_name FROM information_schema.columns WHERE column_default IS NOT NULL AND ' . (string) $condition, $condition->arguments());
    foreach ($columns as $column) {
      $this->connection->query('EXEC sp_rename :old, :new, :type', array(
        ':old' => $old_table_info['schema'] . '.' . $old_table_info['table'] . '_' . $column->column_name . '_df',
        ':new' => $new_table_info['table'] . '_' . $column->column_name . '_df',
        ':type' => 'OBJECT',
      ));
    }
  }

  /**
   * Override DatabaseSchema::dropTable().
   *
   * @status tested
   */
  public function dropTable($table) {
    if (!$this->tableExists($table)) {
      return FALSE;
    }

    $this->connection->query('DROP TABLE {' . $table . '}');
    return TRUE;
  }

  /**
   * Override DatabaseSchema::addField().
   *
   * @status complete
   */
  public function addField($table, $field, $spec, $new_keys = array()) {
    if (!$this->tableExists($table)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot add field %table.%field: table doesn't exist.", array('%field' => $field, '%table' => $table)));
    }
    if ($this->fieldExists($table, $field)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot add field %table.%field: field already exists.", array('%field' => $field, '%table' => $table)));
    }

    // If the field is declared NOT NULL, we have to first create it NULL insert
    // the initial data then switch to NOT NULL.
    if (!empty($spec['not null']) && !isset($spec['default'])) {
      $fixnull = TRUE;
      $spec['not null'] = FALSE;
    }

    // Create the field.
    $query = 'ALTER TABLE {' . $table . '} ADD ';
    $query .= $this->createFieldSql($table, $field, $this->processField($spec));
    $this->connection->query($query);

    // Reset the blob cache.
    $this->additionalColumnInformation = NULL;

    // Load the initial data.
    if (isset($spec['initial'])) {
      $this->connection->update($table)
        ->fields(array($field => $spec['initial']))
        ->execute();
    }

    // Switch to NOT NULL now.
    if (!empty($fixnull)) {
      $spec['not null'] = TRUE;
      $this->connection->query('ALTER TABLE {' . $table . '} ALTER COLUMN ' . $this->createFieldSql($table, $field, $this->processField($spec), TRUE));
    }

    // Add the new keys.
    if (isset($new_keys)) {
      $this->recreateTableKeys($table, $new_keys);
    }
  }

  /**
   * Override DatabaseSchema::changeField().
   *
   * @status complete
   */
  public function changeField($table, $field, $field_new, $spec, $new_keys = array()) {
    if (!$this->fieldExists($table, $field)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot change the definition of field %table.%name: field doesn't exist.", array('%table' => $table, '%name' => $field)));
    }
    if (($field != $field_new) && $this->fieldExists($table, $field_new)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot rename field %table.%name to %name_new: target field already exists.", array('%table' => $table, '%name' => $field, '%name_new' => $field_new)));
    }

    // SQL Server supports transactional DDL, so we can just start a transaction
    // here and pray for the best.
    $transaction = $this->connection->startTransaction();

    // Drop the related objects.
    $this->dropFieldRelatedObjects($table, $field);

    // Start by renaming the current column.
    $this->connection->query('EXEC sp_rename :old, :new, :type', array(
      ':old' => $this->connection->prefixTables('{' . $table . '}.' . $field),
      ':new' => $field . '_old',
      ':type' => 'COLUMN',
    ));

    // If the field is declared NOT NULL, we have to first create it NULL insert
    // the initial data then switch to NOT NULL.
    if (!empty($spec['not null']) && !isset($spec['default'])) {
      $fixnull = TRUE;
      $spec['not null'] = FALSE;
    }

    // Create a new field.
    $this->addField($table, $field_new, $spec);

    // Migrate the data over.
    $this->connection->query('UPDATE [{' . $table . '}] SET [' . $field_new . '] = [' . $field . '_old]');

    // Switch to NOT NULL now.
    if ($fixnull) {
      $spec['not null'] = TRUE;
      $this->connection->query('ALTER TABLE {' . $table . '} ALTER COLUMN ' . $this->createFieldSql($table, $field_new, $this->processField($spec), TRUE));
    }

    // Drop the old field.
    $this->dropField($table, $field . '_old');

    // Add the new keys.
    if (isset($new_keys)) {
      $this->recreateTableKeys($table, $new_keys);
    }
  }

  /**
   * Re-create keys associated to a table, called by createField().
   */
  protected function recreateTableKeys($table, $new_keys) {
    if (isset($new_keys['primary key'])) {
      $this->addPrimaryKey($ret, $table, $new_keys['primary key']);
    }
    if (isset($new_keys['unique keys'])) {
      foreach ($new_keys['unique keys'] as $name => $fields) {
        $this->addUniqueKey($ret, $table, $name, $fields);
      }
    }
    if (isset($new_keys['indexes'])) {
      foreach ($new_keys['indexes'] as $name => $fields) {
        $this->addIndex($ret, $table, $name, $fields);
      }
    }
  }

  /**
   * Override DatabaseSchema::dropField().
   *
   * @status complete
   */
  public function dropField($table, $field) {
    if (!$this->fieldExists($table, $field)) {
      return FALSE;
    }

    // Drop the related objects.
    $this->dropFieldRelatedObjects($table, $field);

    $this->connection->query('ALTER TABLE {' . $table . '} DROP COLUMN ' . $field);
    return TRUE;
  }

  /**
   * Drop the related objects of a column (indexes, constraints, etc.).
   *
   * @status complete
   */
  protected function dropFieldRelatedObjects($table, $field) {
    // Fetch the list of indexes referencing this column.
    $indexes = $this->connection->query('SELECT DISTINCT i.name FROM sys.columns c INNER JOIN sys.index_columns ic ON ic.object_id = c.object_id AND ic.column_id = c.column_id INNER JOIN sys.indexes i ON i.object_id = ic.object_id AND i.index_id = ic.index_id WHERE c.object_id = OBJECT_ID(:table) AND c.name = :name', array(
      ':table' => $this->connection->prefixTables('{' . $table . '}'),
      ':name' => $field,
    ));
    foreach ($indexes as $index) {
      $this->connection->query('DROP INDEX [' . $index->name . '] ON [{' . $table . '}]');
    }

    // Fetch the list of constraints referencing this column.
    $constraints = $this->connection->query('SELECT DISTINCT dc.name FROM sys.columns c INNER JOIN sys.default_constraints dc ON dc.parent_object_id = c.object_id AND dc.parent_column_id = c.column_id WHERE c.object_id = OBJECT_ID(:table) AND c.name = :name', array(
      ':table' => $this->connection->prefixTables('{' . $table . '}'),
      ':name' => $field,
    ));
    foreach ($constraints as $constraint) {
      $this->connection->query('ALTER TABLE [{' . $table . '}] DROP CONSTRAINT [' . $constraint->name . ']');
    }

    // TODO: we might need to drop computed columns when we have some.
  }

  /**
   * Override DatabaseSchema::fieldSetDefault().
   *
   * @status complete
   */
  public function fieldSetDefault($table, $field, $default) {
    if (!$this->fieldExists($table, $field)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot set default value of field %table.%field: field doesn't exist.", array('%table' => $table, '%field' => $field)));
    }

    if ($default === NULL) {
      $default = 'NULL';
    }
    elseif (is_string($default)) {
      $default = "'" . addslashes($spec['default']) . "'";      
    }

    // Try to remove any existing default first.
    try { $this->fieldSetNoDefault($table, $field); } catch (Exception $e) {}

    // Create the new default.
    $this->connection->query('ALTER TABLE [{' . $table . '}] ADD CONSTRAINT {' . $table . '}_' . $field . '_df DEFAULT ' . $default . ' FOR [' . $field . ']');
  }

  /**
   * Override DatabaseSchema::fieldSetNoDefault().
   *
   * @status complete
   */
  public function fieldSetNoDefault($table, $field) {
    if (!$this->fieldExists($table, $field)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot remove default value of field %table.%field: field doesn't exist.", array('%table' => $table, '%field' => $field)));
    }

    $this->connection->query('ALTER TABLE [{' . $table . '}] DROP CONSTRAINT {' . $table . '}_' . $field . '_df');
  }

  /**
   * Override DatabaseSchema::addPrimaryKey().
   *
   * @status tested
   */
  public function addPrimaryKey($table, $fields) {
    if (!$this->tableExists($table)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot add primary key to table %table: table doesn't exist.", array('%table' => $table)));
    }
    if ($this->primaryKeyExists($table)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot add primary key to table %table: primary key already exists.", array('%table' => $table)));
    }

    $this->connection->query('ALTER TABLE [{' . $table . '}] ADD CONSTRAINT {' . $table . '_pkey} PRIMARY KEY (' . $this->createKeySql($fields) . ')');
  }

  /**
   * Override DatabaseSchema::dropPrimaryKey().
   *
   * @status tested
   */
  public function dropPrimaryKey($table) {
    if (!$this->primaryKeyExists($table)) {
      return FALSE;
    }

    $this->connection->query('ALTER TABLE [{' . $table . '}] DROP CONSTRAINT {' . $table . '_pkey}');
    return TRUE;
  }

  /**
   * Find if a primary key exists.
   *
   * @status tested
   */
  protected function primaryKeyExists($table) {
    $table = $this->connection->prefixTables('{' . $table . '}');
    return (bool) $this->connection->query('SELECT 1 FROM sys.key_constraints WHERE parent_object_id = OBJECT_ID(:table) AND type = :type', array(
      ':table' => $table,
      ':type' => 'PK',
    ))->fetchField();
  }

  /**
   * Build the SQL for the creation of an unique key.
   *
   * @status tested
   */
  protected function addUniqueKeySql($table, $name, $fields) {
    // Build a condition.
    $conditions = array();
    foreach ($fields as $field) {
      if (is_array($field)) {
        $field = $field[0];
      }
      $conditions[] = '[' . $field . '] IS NOT NULL';
    }

    return 'CREATE UNIQUE INDEX ' . $name . '_unique ON [{' . $table . '}] (' . $this->createKeySql($fields) . ') WHERE ' . implode(' AND ', $conditions);
  }

  /**
   * Override DatabaseSchema::addUniqueKey().
   *
   * @status tested
   */
  public function addUniqueKey($table, $name, $fields) {
    if (!$this->tableExists($table)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot add unique key %name to table %table: table doesn't exist.", array('%table' => $table, '%name' => $name)));
    }
    if ($this->uniqueKeyExists($table, $name)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot add unique key %name to table %table: unique key already exists.", array('%table' => $table, '%name' => $name)));
    }

    echo $this->addUniqueKeySql($table, $name, $fields);

    $this->connection->query($this->addUniqueKeySql($table, $name, $fields));
  }

  /**
   * Override DatabaseSchema::dropUniqueKey().
   *
   * @status tested
   */
  public function dropUniqueKey($table, $name) {
    if (!$this->uniqueKeyExists($table, $name)) {
      return FALSE;
    }

    $this->connection->query('DROP INDEX ' . $name . '_unique ON [{' . $table . '}]');
  }

  /**
   * Find if an unique key exists.
   *
   * @status tested
   */
  protected function uniqueKeyExists($table, $name) {
    $table = $this->connection->prefixTables('{' . $table . '}');
    return (bool) $this->connection->query('SELECT 1 FROM sys.indexes WHERE object_id = OBJECT_ID(:table) AND name = :name', array(
      ':table' => $table,
      ':name' => $name . '_unique',
    ))->fetchField();
  }

  /**
   * Override DatabaseSchema::addIndex().
   *
   * @status tested
   */
  public function addIndex($table, $name, $fields) {
    if (!$this->tableExists($table)) {
      throw new DatabaseSchemaObjectDoesNotExistException(t("Cannot add index %name to table %table: table doesn't exist.", array('%table' => $table, '%name' => $name)));
    }
    if ($this->indexExists($table, $name)) {
      throw new DatabaseSchemaObjectExistsException(t("Cannot add index %name to table %table: index already exists.", array('%table' => $table, '%name' => $name)));
    }

    $this->connection->query($this->createIndexSql($table, $name, $fields));
  }

  /**
   * Override DatabaseSchema::dropIndex().
   *
   * @status tested
   */
  public function dropIndex($table, $name) {
    if (!$this->indexExists($table, $name)) {
      return FALSE;
    }

    $this->connection->query('DROP INDEX ' . $name . '_idx ON [{' . $table . '}]');
    return TRUE;
  }

  /**
   * Override DatabaseSchema::indexExists().
   *
   * @status tested
   */
  public function indexExists($table, $name) {
    $table = $this->connection->prefixTables('{' . $table . '}');
    return (bool) $this->connection->query('SELECT 1 FROM sys.indexes WHERE object_id = OBJECT_ID(:table) AND name = :name', array(
      ':table' => $table,
      ':name' => $name . '_idx'
    ))->fetchField();
  }
}

/**
 * @} End of "ingroup schemaapi".
 */

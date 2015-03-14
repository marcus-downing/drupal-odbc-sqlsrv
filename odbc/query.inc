<?php

/**
 * SQL Server-specific implementation of INSERT.
 *
 * SQL Server doesn't supports multi-insert queries, and needs special handling for
 * binary columns.
 */
class InsertQuery_odbc extends InsertQuery {
  private $debug = false;

  private function log($message, $variables, $severity) {
    if (is_null($variables))
      $variables = array();
    $variables['this'] = $this;
    if ($this->debug)
      $variables['threshold'] = WATCHDOG_DEBUG;
    $this->connection->log('InsertQuery_odbc: '.$message, $variables, $severity);
  }

  public function debug() {
    $this->debug = true;
    $this->log('Enabling debug', null, WATCHDOG_DEBUG);
  }

  public function execute() {
    if (!$this->preExecute()) {
      return null;
    }

    // Fetch the list of blobs and sequences used on that table.
    $columnInformation = $this->connection->schema()->queryColumnInformation($this->table);

    // Find out if there is an identity field set in this insert.
    foreach ($this->insertFields as $field) {
      if (isset($columnInformation['identities'][$field])) {
        $this->setIdentity = true;
      }
    }

    // Each insert happens in its own query. However, we wrap it in a transaction
    // so that it is atomic where possible.
    // TODO: Do we want to rename this option?
    if (empty($this->queryOptions['sqlsrv_skip_transactions'])) {
      $transaction = $this->connection->startTransaction();
    }

    if (!empty($this->fromQuery)) {
      $this->log('execute: Insert from query', array('%from' => $this->fromQuery), WATCHDOG_DEBUG);

      // Re-initialize the values array so that we can re-use this query.
      $this->insertValues = array();

      try {
        $queryString = $this->connection->prefixTables((string) $this);
        $arguments = $this->fromQuery->getArguments();

        //  TODO: Understand how the arguments work!!!!!
        //  avoid quoting???
        $this->log('execute: Insert from query "%query" with args:', array('%query' => $queryString, '%arguments' => $arguments), WATCHDOG_DEBUG);
        $queryString = $this->connection->replacePlaceholders($queryString, $arguments);
        $this->connection->PDOExec($queryString);
        return $this->connection->lastInsertId();
      } catch (Exception $e) {
        $this->log('execute: '.get_class($e).' when executing "%sql": '.$e->getMessage(), array('%sql' => $queryString), WATCHDOG_ERROR);

        // This INSERT query failed, rollback everything if we started a transaction earlier.
        if (!empty($transaction))
          $transaction->rollback();

        throw $e;
      }


      /*
      if (ODBC_EMULATE_PREPARES) {
        $queryString = $this->connection->prefixTables((string) $this);
        $arguments = $this->fromQuery->getArguments();

        $this->log('execute: Preparing insert-from-query: %query with args: %args', array('%query' => $queryString, '%args' => $arguments), WATCHDOG_DEBUG);
        $queryString = $this->connection->replacePlaceholders($queryString, $arguments);

        $this->log('execute: Prepared insert-from-query: %query', array('%query' => $queryString), WATCHDOG_DEBUG);
        $stmt = $this->connection->PDOPrepare($queryString);
      } else {
        $stmt = $this->connection->PDOPrepare($this->connection->prefixTables((string) $this));
        // Handle the case of SELECT-based INSERT queries first.
        $values = $this->fromQuery->getArguments();
        $this->log('execute: Binding insert-from-query: %query with args: %args', array('%query' => $queryString, '%args' => $values), WATCHDOG_DEBUG);
        foreach ($values as $key => $value) {
          $stmt->bindParam($key, $values[$key]);
        }
      }

      try {
        $this->log('execute', null, WATCHDOG_INFO);
        // else if ($this->connection->drush)
        $stmt->execute();
        $stmt->closeCursor();
      }
      catch (Exception $e) {
        $this->log(get_class($e).' when executing "%sql" with values %vars: '.$e->getMessage(), array('%sql' => (string) $this, '%vars' => $values), WATCHDOG_ERROR);
        // This INSERT query failed, rollback everything if we started a transaction earlier.
        if (!empty($transaction)) {
          $transaction->rollback();
        }
        // Rethrow the exception.
        throw $e;
      }

      return $this->connection->lastInsertId();
      */
    }

    // Handle the case of full-default queries.
    if (empty($this->fromQuery) && (empty($this->insertFields) || empty($this->insertValues))) {

      // Re-initialize the values array so that we can re-use this query.
      $this->insertValues = array();

      $this->log('Insert default values into \'%table\'', array('%table' => $table), WATCHDOG_DEBUG);
      try {
        $sql = $this->connection->prefixTables('INSERT INTO {' . db_escape_table($this->table) . '} DEFAULT VALUES');
        $this->connection->PDOExec($sql);
      }
      catch (Exception $e) {
        $this->log('execute: '.get_class($e).' when executing "%sql": '.$e->getMessage(), array('%sql' => (string) $this), WATCHDOG_ERROR);

        // This INSERT query failed, rollback everything if we started a transaction earlier.
        if (!empty($transaction))
          $transaction->rollback();

        throw $e;
      }

      return $this->connection->lastInsertId();
    }


    // Handle the normal case
    $queryString = $this->connection->prefixTables((string) $this);
    $this->log('Preparing %query', array('%query' => $queryString), WATCHDOG_DEBUG);

    $inserted = false;
    foreach ($this->insertValues as $insert_index => $insert_values) {
      $values = array();
      foreach ($this->insertFields as $field_index => $field) {
        $key = ':db_insert' . $field_index;
        if (isset($insert_values[$field_index])) {
          $values[$key] = $insert_values[$field_index];
        } else {
          $values[$key] = null;
          $this->log('Missing value for \'%field\' when preparing "%query"', array('%field' => $field, '%query' => (string) $this), WATCHDOG_NOTICE);
        }
      }

      try {
        $this->log('Values: %values', array('%values' => $values), WATCHDOG_DEBUG);
        $insertString = $this->connection->replacePlaceholders($queryString, $values);
        $this->log('Prepared: %sql', array('%sql' => $insertString), WATCHDOG_DEBUG);
        $this->connection->PDOExec($insertString);
        $inserted = true;
      }
      catch (Exception $e) {
        $this->log(get_class($e).' when executing "%sql" with values %values: '.$e->getMessage(), array('%sql' => $queryString, '%values' => $values), WATCHDOG_ERROR);

        if (!empty($transaction))
          $transaction->rollback();
        
        throw $e;
      }
    }

    // Re-initialize the values array so that we can re-use this query.
    $this->insertValues = array();

    if ($inserted)
      return $this->connection->lastInsertId();
    return null;


    /*
    $query = (string) $this;
    $queryString = $this->connection->prefixTables($query);
    $stmt = $this->connection->PDOPrepare($queryString);
    $this->log('Binding "%sql"', array('%sql' => $queryString), WATCHDOG_DEBUG);

    // We use this array to store references to the blob handles.
    // This is necessary because the PDO will otherwise messes up with references.
    $data_values = array();

    foreach ($this->insertValues as $insert_index => $insert_values) {
      $max_placeholder = 0;
      $this->log('Values: %values', array('%values' => $insert_values), WATCHDOG_DEBUG);

      foreach ($this->insertFields as $field_index => $field) {
        $placeholder = ':db_insert' . $max_placeholder;
        if ($this->debug) echo "<p>Assigning placeholder $placeholder to field $field_index<p>";
        $max_placeholder++;
        if (isset($columnInformation['blobs'][$field])) {
          $data_values[$placeholder . $insert_index] = fopen('php://memory', 'a');
          fwrite($data_values[$placeholder . $insert_index], $insert_values[$field_index]);
          rewind($data_values[$placeholder . $insert_index]);

          // Need to consider PDO_ODBC equivalent technique if relevant
          //$stmt->bindParam($placeholder, $data_values[$placeholder . $insert_index], PDO::PARAM_LOB, 0, PDO::SQLSRV_ENCODING_BINARY);
        }
        else {
          $this->log('Value %placeholder at index %index', array('%placeholder' => $placeholder, '%index' => $insert_index), WATCHDOG_DEBUG);
          $data_values[$placeholder . $insert_index] = $insert_values[$field_index];
          $stmt->bindParam($placeholder, $data_values[$placeholder . $insert_index]);
        }
      }

      try {
        $this->log('executing', null, WATCHDOG_INFO);
        $stmt->execute();
      }
      catch (Exception $e) {
        $this->log(get_class($e).' when executing "%sql" with values %vars: '.$e->getMessage(), array('%sql' => (string) $this, '%vars' => $insert_values), WATCHDOG_ERROR);

        // This INSERT query failed, rollback everything if we started a transaction earlier.
        if (!empty($transaction)) {
          $transaction->rollback();
        }
        // Rethrow the exception.
        throw $e;
      }

      $last_insert_id = $this->connection->lastInsertId();
    }

    $stmt->closeCursor();
    // Re-initialize the values array so that we can re-use this query.
    $this->insertValues = array();

    return $last_insert_id;
    */
  }

  public function __toString() {
    $table = db_escape_table($this->table);
    // Create a sanitized comment string to prepend to the query.
    $prefix = $this->connection->makeComment($this->comments);

    // Enable direct insertion to identity columns if necessary.
    if (!empty($this->setIdentity)) {
      $prefix .= 'SET IDENTITY_INSERT {' . $table . '} ON;';
    }

    // If we're selecting from a SelectQuery, finish building the query and
    // pass it back, as any remaining options are irrelevant.
    if (!empty($this->fromQuery)) {
      return $prefix . "INSERT INTO {" . $table . '} (' . implode(', ', $this->connection->quoteIdentifiers($this->insertFields)) . ') ' . $this->fromQuery;
    }

    // Build the list of placeholders.
    $placeholders = array();
    for ($i = 0; $i < count($this->insertFields); ++$i) {
      $placeholders[] = ':db_insert' . $i;
    }

    return $prefix . 'INSERT INTO {' . $table . '} (' . implode(', ', $this->connection->quoteIdentifiers($this->insertFields)) . ') VALUES (' . implode(', ', $placeholders) . ')';
  }
}

/**
 * SQL Server-specific implementation of UPDATE.
 *
 * The specific parts are:
 *  - SQL Server returns the number of matched rows to an UPDATE, and Drupal
 *    requires the number of affected rows to be returned.
 *  - SQL Server requires a special handling for the blobs.
 */
class UpdateQuery_odbc extends UpdateQuery {
  private $debug = false;

  private function log($message, $variables, $severity) {
    if (is_null($variables))
      $variables = array();
    $variables['this'] = $this;
    if ($this->debug)
      $variables['threshold'] = WATCHDOG_DEBUG;
    $this->connection->log('UpdateQuery_odbc: '.$message, $variables, $severity);
  }

  public function debug() {
    $this->debug = true;
    $this->log('Enabling debug');
  }


  /**
   * Rewrite the query not to select non-affected rows.
   *
   * A query like this one:
   *   UPDATE test SET col1 = 'newcol1', col2 = 'newcol2' WHERE tid = 1
   * will become:
   *   UPDATE test SET col1 = 'newcol1', col2 = 'newcol2' WHERE tid = 1 AND (col1 <> 'newcol1' OR col2 <> 'newcol2')
   */
  protected function excludeNonAffectedRows() {
    if (!empty($this->queryOptions['sqlsrv_return_matched_rows'])) {
      return;
    }

    // Get the fields used in the update query.
    $fields = $this->expressionFields + $this->fields;

    // Add the inverse of the fields to the condition.
    $condition = new DatabaseCondition('OR');
    foreach ($fields as $field => $data) {
      if (is_array($data)) {
        // The field is an expression.
        // Re-bind the placeholders.
        $expression = $data['expression'];
        $arguments = array();
        if (!empty($data['arguments'])) {
          foreach ($data['arguments'] as $placeholder => $value) {
            $new_placeholder = ':db_exclude_placeholder_' . $this->nextPlaceholder();
            $expression = str_replace($placeholder, $new_placeholder, $expression);
            $arguments[$new_placeholder] = $value;
          }
        }
        $condition->where($field . ' <> ' . $expression, $arguments);
        $condition->isNull($field);
      }
      elseif (!isset($data)) {
        // The field will be set to null.
        $condition->isNotNull($field);
      }
      else {
        $condition->condition($field, $data, '<>');
        $condition->isNull($field);
      }
    }
    if (count($condition)) {
      // Workaround for a bug in the base MergeQuery implementation:
      // a DatabaseCondition object is reused without being re-compiled,
      // leading to duplicate placeholders.
      $this->nextPlaceholder = 1000000;
      $this->condition($condition);
    }
  }

  public function execute() {
    // Rewrite the query to exclude the non-affected rows.
    // $this->excludeNonAffectedRows();

    // Now perform the special handling for BLOB fields.
    $max_placeholder = 0;


    $queryString = $this->connection->prefixTables((string)$this);
    $this->log('execute: Preparing query "%query"', array('%query' => $queryString), WATCHDOG_DEBUG);

    // $columnInformation = $this->connection->schema()->queryColumnInformation($this->table);

    // Expressions take priority over literal fields, so we process those first
    // and remove any literal fields that conflict.
    $fields = $this->fields;
   
    // foreach ($this->expressionFields as $field => $data) {
    //   if (!empty($data['arguments'])) {
    //     foreach ($data['arguments'] as $placeholder => $argument) {
    //       // We assume that an expression will never happen on a BLOB field,
    //       // which is a fairly safe assumption to make since in most cases
    //       // it would be an invalid query anyway.
    //       $stmt->bindParam($placeholder, $data['arguments'][$placeholder]);
    //     }
    //   }
    //   unset($fields[$field]);
    // }

    $values = array();
    foreach ($fields as $field => $value) {
      $placeholder = ':db_update_placeholder_' . ($max_placeholder++);

      // if (isset($columnInformation['blobs'][$field])) {
      //   // fail at blobs
      //   continue;
      // }
      $values[$placeholder] = $value;
    }
    $this->log('execute: Values: %args', array('%args' => $values), WATCHDOG_DEBUG);
    // $updateString = $this->connection->replacePlaceholders($queryString, $values);

    if (count($this->condition)) {
      $this->condition->compile($this->connection, $this);

      $arguments = $this->condition->arguments();
      // foreach ($arguments as $placeholder => $value) {
      //   $stmt->bindParam($placeholder, $arguments[$placeholder]);
      // }
      // $updateString = $this->connection->replacePlaceholders($updateString, $arguments);
      $this->log('execute: Arguments: %args', array('%args' => $arguments), WATCHDOG_DEBUG);

      if ($intersection = array_intersect_key($values, $arguments))
        $this->log('execute: Conflict between values and arguments! %intersection', array('%intersection' => $intersection), WATCHDOG_DEBUG);
      $values = array_merge($values, $arguments);
    }

    try {
      $updateString = $this->connection->replacePlaceholders($queryString, $values);
      $this->log('execute: Prepared: "%query"', array('%query' => $updateString), WATCHDOG_DEBUG);

      $stmt = $this->connection->PDOPrepare($updateString);
      $stmt->execute();

      $nrows = $stmt->rowCount();
      $stmt->closeCursor();
      return $nrows;
    } catch (Exception $e) {
      $this->log(get_class($e).' when executing "%sql" with variables %vars: %error', array('%sql' => (string) $this, '%vars' => $values, '%error' => $e->getMessage()), WATCHDOG_ERROR);

      if (!empty($transaction))
        $transaction->rollback();
      
      throw $e;
    }

    /*
    }

    // Because we filter $fields the same way here and in __toString(), the
    // placeholders will all match up properly.
    $stmt = $this->connection->PDOPrepare($this->connection->prefixTables((string)$this));

    // Fetch the list of blobs and sequences used on that table.
    $columnInformation = $this->connection->schema()->queryColumnInformation($this->table);

    // Expressions take priority over literal fields, so we process those first
    // and remove any literal fields that conflict.
    $fields = $this->fields;
    $expression_fields = array();
    foreach ($this->expressionFields as $field => $data) {
      if (!empty($data['arguments'])) {
        foreach ($data['arguments'] as $placeholder => $argument) {
          // We assume that an expression will never happen on a BLOB field,
          // which is a fairly safe assumption to make since in most cases
          // it would be an invalid query anyway.
          $stmt->bindParam($placeholder, $data['arguments'][$placeholder]);
        }
      }
      unset($fields[$field]);
    }

    // We use this array to store references to the blob handles.
    // This is necessary because the PDO will otherwise messes up with references.
    $blobs = array();
    $blob_count = 0;

    foreach ($fields as $field => $value) {
      $placeholder = ':db_update_placeholder_' . ($max_placeholder++);

      if (isset($columnInformation['blobs'][$field])) {
        $blobs[$blob_count] = fopen('php://memory', 'a');
        fwrite($blobs[$blob_count], $value);
        rewind($blobs[$blob_count]);
        // Again, do we need a PDO_ODBC specific way of dealing with binary encoings
        //$stmt->bindParam($placeholder, $blobs[$blob_count], PDO::PARAM_LOB, 0, PDO::SQLSRV_ENCODING_BINARY);
        $blob_count++;
      }
      else {
        $stmt->bindParam($placeholder, $fields[$field]);
      }
    }

    if (count($this->condition)) {
      $this->condition->compile($this->connection, $this);

      $arguments = $this->condition->arguments();
      foreach ($arguments as $placeholder => $value) {
        $stmt->bindParam($placeholder, $arguments[$placeholder]);
      }
    }

    $options = $this->queryOptions;
    $options['already_prepared'] = true;
    $stmt->execute();

    $nrows = $stmt->rowCount();
    $stmt->closeCursor();
    return $nrows;
    */
  }

  public function __toString() {
    // Create a sanitized comment string to prepend to the query.
    $prefix = $this->connection->makeComment($this->comments);

    // Expressions take priority over literal fields, so we process those first
    // and remove any literal fields that conflict.
    $fields = $this->fields;
    $update_fields = array();
    foreach ($this->expressionFields as $field => $data) {
      $update_fields[] = $this->connection->quoteIdentifier($field) . '=' . $data['expression'];
      unset($fields[$field]);
    }

    $max_placeholder = 0;
    foreach ($fields as $field => $value) {
      $update_fields[] = $this->connection->quoteIdentifier($field) . '=:db_update_placeholder_' . ($max_placeholder++);
    }

    $query = $prefix . 'UPDATE {' . $this->connection->escapeTable($this->table) . '} SET ' . implode(', ', $update_fields);

    if (count($this->condition)) {
      $this->condition->compile($this->connection, $this);
      // There is an implicit string cast on $this->condition.
      $query .= "\nWHERE " . $this->condition;
    }

    return $query;
  }
}

/**
 * SQL Server-specific implementation of TRUNCATE.
 */
class TruncateQuery_odbc extends TruncateQuery {

  public function __toString() {
    // Create a sanitized comment string to prepend to the query.
    $prefix = $this->connection->makeComment($this->comments);

    return $prefix . 'TRUNCATE TABLE {' . $this->connection->escapeTable($this->table) . '} ';
  }
}

/**
 * SQL Server-specific implementation of the MERGE operation.
 */
class MergeQuery_odbc extends MergeQuery {
  
  public function execute() {
    // We don't need INSERT or UPDATE queries to trigger additional transactions.
    // TODO: Do we want to rename this option
    $this->queryOptions['sqlsrv_skip_transactions'] = true;

    return parent::execute();
  }
}

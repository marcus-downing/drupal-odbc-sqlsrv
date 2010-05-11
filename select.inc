<?php

class SelectQuery_sqlsrv extends SelectQuery {
  public function __toString() {
    // SELECT
    $query = 'SELECT ';
    if ($this->distinct) {
      $query .= 'DISTINCT ';
    }

    // FIELDS and EXPRESSIONS
    $fields = array();
    foreach ($this->tables as $alias => $table) {
      if (!empty($table['all_fields'])) {
        $fields[] = $alias . '.*';
      }
    }
    foreach ($this->fields as $alias => $field) {
      // Always use the AS keyword for field aliases, as some
      // databases require it (e.g., PostgreSQL).
      $fields[] = (isset($field['table']) ? $field['table'] . '.' : '') . $field['field'] . ' AS ' . $field['alias'];
    }
    foreach ($this->expressions as $alias => $expression) {
      $fields[] = $expression['expression'] . ' AS ' . $expression['alias'];
    }
    $query .= implode(', ', $fields);


    // FROM - We presume all queries have a FROM, as any query that doesn't won't need the query builder anyway.
    $query .= "\nFROM ";
    foreach ($this->tables as $alias => $table) {
      $query .= "\n";
      if (isset($table['join type'])) {
        $query .= $table['join type'] . ' JOIN ';
      }

      // If the table is a subquery, compile it and integrate it into this query.
      if ($table['table'] instanceof SelectQueryInterface) {
        $table_string = '(' . (string)$table['table'] . ')';
      }
      else {
        $table_string = '{' . $this->connection->escapeTable($table['table']) . '}';
      }

      // Don't use the AS keyword for table aliases, as some
      // databases don't support it (e.g., Oracle).
      $query .=  $table_string . ' ' . $table['alias'];

      if (!empty($table['condition'])) {
        $query .= ' ON ' . $table['condition'];
      }
    }

    // WHERE
    if (count($this->where)) {
      $this->where->compile($this->connection, $this);
      // There is an implicit string cast on $this->condition.
      $query .= "\nWHERE " . $this->where;
    }

    // GROUP BY
    if ($this->group) {
      $query .= "\nGROUP BY " . implode(', ', $this->group);
    }

    // HAVING
    if (count($this->having)) {
      $this->having->compile($this->connection, $this);
      // There is an implicit string cast on $this->having.
      $query .= "\nHAVING " . $this->having;
    }

    // ORDER BY
    if ($this->order) {
      $query .= "\nORDER BY ";
      $fields = array();
      foreach ($this->order as $field => $direction) {
        $fields[] = $field . ' ' . $direction;
      }
      $query .= implode(', ', $fields);
    }

    // RANGE
    if (!empty($this->range)) {
      $query = $this->connection->addRangeToQuery($query, $this->range['start'], $this->range['length']);
    }

    // UNION is a little odd, as the select queries to combine are passed into
    // this query, but syntactically they all end up on the same level.
    if ($this->union) {
      foreach ($this->union as $union) {
        $query .= ' ' . $union['type'] . ' ' . (string) $union['query'];
      }
    }

    return $query;
  }

  /**
   * Temporary override of SelectQuery::addJoin().
   *
   * Sanitize the alias.
   */
  public function addJoin($type, $table, $alias = NULL, $condition = NULL, $arguments = array()) {
    if (empty($alias)) {
      if ($table instanceof SelectQueryInterface) {
        $alias = 'subquery';
      }
      else {
        // Do not use DatabaseConnection::escapeTable() here: we don't want
        // the # prefix to get thru.
        $alias = preg_replace('/[^A-Za-z0-9_.]+/', '', $table);
      }
    }

    $alias_candidate = $alias;
    $count = 2;
    while (!empty($this->tables[$alias_candidate])) {
      $alias_candidate = $alias . '_' . $count++;
    }
    $alias = $alias_candidate;

    $this->tables[$alias] = array(
      'join type' => $type,
      'table' => $table,
      'alias' => $alias,
      'condition' => $condition,
      'arguments' => $arguments,
    );

    return $alias;
  }

  /**
   * Override of SelectQuery::orderRandom() for SQL Server.
   *
   * It seems that sorting by RAND() doesn't actually work, this is a less then
   * elegant workaround.
   *
   * @status tested
   */
  public function orderRandom() {
    $alias = $this->addExpression('NEWID()', 'random_field');
    $this->orderBy($alias);
    return $this;
  }
}
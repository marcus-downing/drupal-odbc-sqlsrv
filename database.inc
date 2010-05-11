<?php
// $Id $

/**
 * @file
 * Database interface code for Microsoft SQL Server.
 */

/**
 * @ingroup database
 * @{
 */

include_once DRUPAL_ROOT . '/includes/database/prefetch.inc';

class DatabaseConnection_sqlsrv extends DatabaseConnection {

  /**
   * Override of DatabaseConnection::driver().
   *
   * @status tested
   */
  public function driver() {
    return 'sqlsrv';
  }

  /**
   * Override of DatabaseConnection::databaseType().
   *
   * @status tested
   */
  public function databaseType() {
    return 'sqlsrv';
  }

  /**
   * Override of DatabaseConnection::databaseType().
   *
   * @status complete
   */
  public function __construct(array $connection_options = array()) {
    // Store connection options for future reference.
    $this->connectionOptions = $connection_options;

    // We don't need a specific PDOStatement class here, we simulate it using
    // DatabaseStatement_sqlsrv below.
    $this->statementClass = NULL;

    // This driver defaults to transaction support, except if explicitly passed FALSE.
    $this->transactionSupport = !isset($connection_options['transactions']) || $connection_options['transactions'] !== FALSE;

    // Launch the connection to the server.
    PDO::__construct('sqlsrv:' . $connection_options['host'], $connection_options['username'], $connection_options['password'], array(
      'Database' => $connection_options['database'],
    ));

    // Because the other methods don't seem to work right.
    $this->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    // Convert numeric values to strings when fetching.
    $this->setAttribute(PDO::ATTR_STRINGIFY_FETCHES, TRUE);
    // Force column names to lower case.
    $this->setAttribute(PDO::ATTR_CASE, PDO::CASE_LOWER);
  }

  /**
   * Override of PDO::prepare(): prepare a prefetching database statement.
   *
   * @status tested
   */
  public function prepare($query, $options = array()) {
    return new DatabaseStatement_sqlsrv($this, $query, $options);
  }

  /**
   * Temporary override of DatabaseConnection::prepareQuery().
   *
   * @todo: remove that when DatabaseConnection::prepareQuery() is fixed to call
   *   $this->prepare() and not parent::prepare().
   * @status: tested, temporary
   */
  public function prepareQuery($query) {
    $query = $this->prefixTables($query);

    // Call our overriden prepare.
    return $this->prepare($query);
  }

  /**
   * Internal function: prepare a query by calling PDO directly.
   *
   * This function has to be public because it is called by other parts of the
   * database layer, but do not call it directly, as you risk locking down the
   * PHP process.
   */
  public function PDOPrepare($query, array $options = array()) {
    $query = $this->preprocessQuery($query);
    return parent::prepare($query, $options);
  }

  /**
   * This is the original replacement regexp from Microsoft.
   *
   * We could probably simplify it a lot because queries only contain
   * placeholders when we modify them.
   *
   * NOTE: removed 'escape' from the list, because it explodes
   * with LIKE xxx ESCAPE yyy syntax.
   */
  const RESERVED_REGEXP = '/\G 
    \b(?<![:\[_])(?:
      (action|admin|alias|any|are|array|at|begin|boolean|class|commit|contains|current|data|date|day|depth|domain|end|external|file|full|function|get|go|host|input|language|last|less|local|map|min|module|new|no|object|old|open|operation|parameter|parameters|path|plan|prefix|proc|public|ref|result|returns|role|row|rows|save|search|second|section|session|size|state|statistics|temporary|than|time|timestamp|tran|translate|translation|trim|user|value|variable|view|without)
      |
      ([a-z]+)
    )\b
    |
    \b(
      [^a-z\'"\\\\]+
    )\b
    |
    (?=[\'"])
    (
      "  [^\\\\"] * (?: \\\\. [^\\\\"] *) * "
      |
      \' [^\\\\\']* (?: \\\\. [^\\\\\']*) * \'
    ) 
  /Six';

  protected function replaceReservedCallback($matches) {
    if ($matches[1] !== '') {
      // Replace reserved words.
      return '[' . $matches[1] . ']';
    }
    else {
      // Let other value passthru.
      for ($i = 2; $i < count($matches); $i++) {
        if ($matches[$i] !== '') {
          return $matches[$i];
        }
      }
    }
    return '';
  }

  /**
   * Override of DatabaseConnection::prefixTables().
   *
   * @status tested
   */
  public function prefixTables($sql) {
    // Implements a simpler (and probably faster) method for prefix replacements.
    return preg_replace_callback('/{([^}]*)}/', array($this, 'prefixTablesCallback'), $sql);
  }

  /**
   * Internal use: replacement callback for DatabaseConnection_sqlsrv::prefixTable().
   *
   * @status tested
   */
  protected function prefixTablesCallback($matches) {
    global $db_prefix;

    $table = $matches[1];

    // Don't prefix temporary tables.
    if ($table[0] == '#') {
      return $table;
    }

    // Else, do normal prefixing.
    if (is_array($db_prefix)) {
      if (isset($db_prefix[$table])) {
        $prefix = $db_prefix[$table];
      }
      else {
        $prefix = $db_prefix['default'];
      }
    }
    else {
      $prefix = $db_prefix;
    }

    return $prefix . $table;
  }

  public function quoteIdentifier($identifier) {
    return '[' . $identifier .']';
  }

  public function quoteIdentifiers($identifiers) {
    return array_map(array($this, 'quoteIdentifier'), $identifiers);;
  }

  /**
   * Override of DatabaseConnection::queryRange().
   */
  public function queryRange($query, $from, $count, array $args = array(), array $options = array()) {
    $query = $this->addRangeToQuery($query, $from, $count);
    return $this->query($query, $args, $options);
  }

  /**
   * Override of DatabaseConnection::queryTemporary().
   *
   * @status tested
   */
  public function queryTemporary($query, array $args = array(), array $options = array()) {
    $tablename = $this->generateTemporaryTableName();

    // Replace SELECT xxx FROM table by SELECT xxx INTO #table FROM table.
    $query = preg_replace('/^SELECT(.*?)FROM/i', 'SELECT$1 INTO #' . $tablename . ' FROM', $query);

    // Prefix tables.
    $query = $this->prefixTables($query);

    // Manually replace placeholders and directly execute the query.
    // SQL Server requires us to directly execute the query because the temporary
    // table would be automatically flushed at the end of the prepared statement
    // execution.
    $query = $this->preprocessQuery($query);
    $query = $this->replacePlaceholders($query, $args);
    PDO::exec($query);
    return '#' . $tablename;
  }

  /**
   * Internal function: do a manual placeholder replacement for a query.
   */
  protected function replacePlaceholders($query, array $args = array()) {
    // Check if $args is a simple numeric array.
    if (range(0, count($args) - 1) === array_keys($args)) {
      // In that case, we have unnamed placeholders.
      $count = 0;
      $new_args = array();
      foreach ($args as $value) {
        if (is_float($value)) {
          // Force the conversion to float so as not to loose precision
          // in the automatic cast.
          $value = sprintf('%F', $value);
        }
        elseif (!is_int($value)) {
          $value = $this->quote($value);
        }
        $query = substr_replace($query, $value, strpos($query, '?'), 1);
      }
      $args = $new_args;
    }
    else {
      // Else, this is using named placeholders.
      foreach ($args as $placeholder => $value) {
        if (is_float($value)) {
          // Force the conversion to float so as not to loose precision
          // in the automatic cast.
          $value = sprintf('%F', $value);
        }
        elseif (!is_int($value)) {
          $value = $this->quote((string) $value);
        }

        // PDO allows placeholders to not be prefixed by a colon. See
        // http://marc.info/?l=php-internals&m=111234321827149&w=2 for
        // more.
        if ($placeholder[0] != ':') {
          $placeholder = ":$placeholder";
        }
        // When replacing the placeholders, make sure we search for the
        // exact placeholder. For example, if searching for
        // ':db_placeholder_1', do not replace ':db_placeholder_11'.
        $query = preg_replace('/' . preg_quote($placeholder) . '\b/', $value, $query);
      }
    }
    return $query;
  }

  /**
   * Internal function: massage a query to make it compliant with SQL Server.
   */
  protected function preprocessQuery($query) {
    // Force quotes around some SQL Server reserved keywords.
    if (preg_match('/^SELECT/', $query)) {
      $query = preg_replace_callback(self::RESERVED_REGEXP, array($this, 'replaceReservedCallback'), $query);
    }

    // Last chance to modify some SQL Server-specific syntax.
    $replacements = array(
      // Normalize SAVEPOINT syntax to the SQL Server one.
      '/^SAVEPOINT (.*)$/' => 'SAVE TRANSACTION $1',
      '/^ROLLBACK TO SAVEPOINT (.*)$/' => 'ROLLBACK TRANSACTION $1',
      // SQL Server doesn't need an explicit RELEASE SAVEPOINT.
      '/^RELEASE SAVEPOINT (.*)$/' => '-- $0',
    );
    $query = preg_replace(array_keys($replacements), $replacements, $query);

    // Add prefixes to Drupal-specific functions.
    $functions = array(
      'SUBSTRING_INDEX',
      'GREATEST',
    );
    foreach ($functions as $function) {
      $query = preg_replace('/\b(?<!:)(' . preg_quote($function) . ')\b/i', 'dbo.$1', $query);
    }

    $replacements = array(
      'LENGTH' => 'LEN',
      'POW' => 'POWER',
    );
    foreach ($replacements as $function => $replacement) {
      $query = preg_replace('/\b(?<!:)(' . preg_quote($function) . ')\(/i', $replacement . '(', $query);
    }

    // Replace the ANSI concatenation operator with SQL Server poor one.
    $query = preg_replace('/\|\|/', '+', $query);

    return $query;
  }

  /**
   * Internal function: add range options to a query.
   *
   * This cannot be set protected because it is used in other parts of the
   * database engine.
   *
   * @status tested
   */
  public function addRangeToQuery($query, $from, $count) {
    if ($from == 0) {
      // Easy case: just use a TOP query if we don't have to skip any rows.
      $query = preg_replace('/^\s*SELECT(\s*DISTINCT)?/Dsi', 'SELECT$1 TOP(' . $count . ')', $query); 
    }
    else {
      // More complex case: use a TOP query to retrieve $from + $count rows, and
      // filter out the first $from rows using a window function.
      $query = preg_replace('/^\s*SELECT(\s*DISTINCT)?/Dsi', 'SELECT$1 TOP(' . ($from + $count) . ') ', $query);  
      $query = '
        SELECT * FROM (
          SELECT sub2.*, ROW_NUMBER() OVER(ORDER BY sub2._line2) AS _line3 FROM (
            SELECT 1 AS _line2, sub1.* FROM (' . $query . ') AS sub1
          ) as sub2
        ) AS sub3 
        WHERE _line3 BETWEEN ' . ($from + 1) . ' AND ' . ($from + $count);
    }

    return $query;
  }

  public function mapConditionOperator($operator) {
    // We don't want to override any of the defaults.
    static $specials = array(
      'LIKE' => array('postfix' => " ESCAPE '!'"),
      'NOT LIKE' => array('postfix' => " ESCAPE '!'"),
    );
    return isset($specials[$operator]) ? $specials[$operator] : NULL;
  }

  /**
   * Temporary override of DatabaseConnection::escapeLike().
   *
   * There is a bug in the PDO_sqlsrv driver in which it fails to properly
   * replace ESCAPE '\' statements.
   */
  public function escapeLike($string) {
    // Use the '!' character instead of '\'
    return str_replace(array('!', '%', '_'), array('!!', '!%', '!_'), $string);
  }

  /**
   * Override of DatabaseConnection::nextId().
   *
   * @status tested
   */
  public function nextId($existing = 0) {
    // If an exiting value is passed, for its insertion into the sequence table.
    if ($existing > 0) {
      try {
        $this->query('SET IDENTITY_INSERT {sequences} ON; INSERT INTO {sequences} (value) VALUES(:existing); SET IDENTITY_INSERT {sequences} OFF', array(':existing' => $existing));
      }
      catch (Exception $e) {
        // Doesn't matter if this fails, it just means that this value is already
        // present in the table.
      }
    }

    return $this->query('INSERT INTO {sequences} DEFAULT VALUES', array(), array('return' => Database::RETURN_INSERT_ID));
  }

  /**
   * Override DatabaseConnection::escapeTable().
   *
   * @status needswork
   */
  public function escapeTable($table) {
    // Rescue the # prefix from the escaping.
    return ($table[0] == '#' ? '#' : '') . preg_replace('/[^A-Za-z0-9_.]+/', '', $table);
  }
}

class DatabaseStatement_sqlsrv extends DatabaseStatementPrefetch implements Iterator, DatabaseStatementInterface {
  protected function getStatement($query, &$args = array()) {
    return $this->dbh->PDOPrepare($query);
  }

  public function execute($args = array(), $options = array()) {
    if (isset($options['fetch'])) {
      if (is_string($options['fetch'])) {
        // Default to an object. Note: db fields will be added to the object
        // before the constructor is run. If you need to assign fields after
        // the constructor is run, see http://drupal.org/node/315092.
        $this->setFetchMode(PDO::FETCH_CLASS, $options['fetch']);
      }
      else {
        $this->setFetchMode($options['fetch']);
      }
    }

    $logger = $this->dbh->getLogger();
    if (!empty($logger)) {
      $query_start = microtime(TRUE);
    }

    // Prepare the query.
    $statement = $this->getStatement($this->queryString, $args);
    if (!$statement) {
      $this->throwPDOException();
    }

    $return = $statement->execute($args);
    if (!$return) {
      $this->throwPDOException();
    }

    // Fetch all the data from the reply, in order to release any lock
    // as soon as possible.
    $this->rowCount = $statement->rowCount();

    // Bind the binary columns properly.
    for ($i = 0; $i < $statement->columnCount(); $i++) {
      $meta = $statement->getColumnMeta($i);
      if ($meta['sqlsrv:decl_type'] == 'varbinary') {
        $statement->bindColumn($i + 1, $null, PDO::PARAM_LOB, 0, PDO::SQLSRV_ENCODING_BINARY);
      }
    }

    try {
      $this->data = $statement->fetchAll(PDO::FETCH_ASSOC);
    }
    catch (Exception $e) {
      $this->data = array();
    }

    $this->resultRowCount = count($this->data);

    if ($this->resultRowCount) {
      $this->columnNames = array_keys($this->data[0]);
    }
    else {
      $this->columnNames = array();
    }

    if (!empty($logger)) {
      $query_end = microtime(TRUE);
      $logger->log($this, $args, $query_end - $query_start);
    }

    // Remove the _line2 and _line3 internal columns from the result set.
    $drop_columns = FALSE;
    foreach ($this->columnNames as $k => $column) {
      if ($column == '_line2' || $column == '_line3') {
        $drop_columns = TRUE;
        unset($this->columnNames[$k]);
      }
    }

    if ($drop_columns) {
      // Renumber columns.
      $this->columnNames = array_values($this->columnNames);

      foreach ($this->data as $k => $row) {
        unset($this->data[$k]['_line2']);
        unset($this->data[$k]['_line3']);
      }
    }

    // Destroy the statement as soon as possible.
    unset($statement);

    // Initialize the first row in $this->currentRow.
    $this->next();

    return $return;
  }
}

/**
 * @} End of "ingroup database".
 */

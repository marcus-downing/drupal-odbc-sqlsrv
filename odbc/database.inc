<?php

/**
 * @file
 * Database interface code for ODBC PDO driver.
 */

/**
 * @ingroup database
 * @{
 */

include_once DRUPAL_ROOT . '/includes/database/prefetch.inc';
include_once __DIR__ . '/unicode.inc';

// can't always take constants for granted across PHP versions, so define these if they're not already
if (!defined('WATCHDOG_ERROR'))     define('WATCHDOG_ERROR', 3);
if (!defined('WATCHDOG_WARNING'))   define('WATCHDOG_WARNING', 4);
if (!defined('WATCHDOG_NOTICE'))    define('WATCHDOG_NOTICE', 5);
if (!defined('WATCHDOG_INFO'))      define('WATCHDOG_INFO', 5);

if (!defined('ENT_HTML401'))        define('ENT_HTML401', 0);
if (!defined('ENT_HTML5'))          define('ENT_HTML5', 16 | 32);
if (!defined('ENT_NOQUOTES'))       define('ENT_NOQUOTES', 0);
if (!defined('ENT_IGNORE'))         define('ENT_IGNORE', 4);

/**
 * Emulate ATTR_EMULATE_PREPARES on drivers that don't support it
 */
if (!defined('ODBC_EMULATE_PREPARES')) define('ODBC_EMULATE_PREPARES', true);

/**
 * Swallow some exceptions and log the errors instead
 */
if (!defined('ODBC_FORGIVING')) define('ODBC_FORGIVING', true);

/**
 * Whether to use transactions with the SQL connection.
 *
 * This can be overridden in the connection settings with 'transations' => (boolean)
 */
if (!defined('ODBC_USE_TRANSACTIONS')) define('ODBC_USE_TRANSACTIONS', false);

/**
 * Use the deprecated but more compatible `text` data type in place of `varchar` and `varbinary`
 *
 * This can be overridden in the connection setttings with 'use_text' => (boolean)
 */
if (!defined('ODBC_USE_TEXT')) define('ODBC_USE_TEXT', true);

/**
 * Use this encoding to store Unicode text on a non-Unicode database.
 *
 *  ''                  = No encoding, use UTF-8 directly          £
 *  'html'              = HTML character entities                  &#163;
 *  'u'                 = Unicode escaped characters               \u00A3
 *  'url'               = URL-encoded characters                   %C2%A3
 *  'quoted-printable'  = Use email "quoted-printable" encoding    =C2=A3
 *
 * This can be overridden in the connection settings with 'encoding' => (string)
 */
if (!defined('ODBC_ENCODING')) define('ODBC_ENCODING', 'u');

/**
 * Deeper debugging. Do not leave this flag switch on!
 *
 * For debugging of your own code, set 'debug' => true flag on the database connection settings.
 * 
 * 0 = None
 * 1 = Lots of debugging spewed all over your screen
 * 2 = A totally unreasonable amount of debugging
 */
if (!defined('ODBC_WATCHDOG_DEBUG')) define('ODBC_WATCHDOG_DEBUG', 0);



class DatabaseConnection_odbc extends DatabaseConnection {

  public $bypassQueryPreprocess = FALSE;

  private $debug = FALSE;

  private $watchdogQueue = array();

  /**
   * Override of DatabaseConnection::driver().
   *
   * @status tested
   */
  public function driver() {
    return 'odbc';
  }

  /**
   * Override of DatabaseConnection::databaseType().
   *
   * @status tested
   */
  public function databaseType() {
    return 'odbc';
  }

  /**
   * Override of DatabaseConnection::version().
   */
  public function version() {
    $version = $this->query('SELECT @@VERSION')->fetchField();
    $this->watchdog('Server version: %version', array('%version' => $version));
    return $version;
  }

  /**
   * Log messages with dblog; or queue them if we're in the middle of another query
   */
  public function watchdog($message, $variables = array(), $severity = WATCHDOG_NOTICE, $link = '') {
    if (!$this->debug)
      return;
    if (ODBC_WATCHDOG_DEBUG) {
      echo "<p><b>Watchdog:</b> $message</p>\n<pre>"; print_r($variables); echo "</pre>\n";
      if (ODBC_WATCHDOG_DEBUG >= 2) {
        echo "<p>Transaction depth: ".$this->transactionDepth()."</p>";
        echo "<pre style='font-size: 0.7em; padding: 0 1em;'>"; debug_print_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS); echo "</pre>\n";
      }
    }
    if ($this->inTransaction()) {
      $this->watchdogQueue[] = array($message, $variables, $severity, $link);
    } else {
      watchdog('odbc-sqlsrv', $message, $variables, $severity, $link);
    }
  }

  public function popTransaction($name) {
    parent::popTransaction($name);
    if (!$this->inTransaction()) {
      foreach ($this->watchdogQueue as list($message, $variables, $severity, $link)) {
        watchdog('odbc-sqlsrv', $message, $variables, $severity, $link);
      }
      $this->watchdogQueue = array();
    }
  }

  /**
   * Override of DatabaseConnection::databaseType().
   *
   * @status complete
   */
  public function __construct(array $connection_options = array()) {
    // Whether to use dblog / watchdog to record debug information
    $this->debug = isset($connection_options['debug']) && (boolean) $connection_options['debug'] && is_callable('watchdog');

    // Store connection options for future reference.
    $this->connectionOptions = $connection_options;

    // We don't need a specific PDOStatement class here, we simulate it using
    // DatabaseStatement_odbc below.
    $this->statementClass = NULL;

    // Whether to use transactions
    $this->transactionSupport = isset($connection_options['transactions']) ? (boolean) $connection_options['transactions'] : ODBC_USE_TRANSACTIONS;

    // Whether to use `text` fields instead of varchar(max) and varbinary(max)
    $this->useTextFields = isset($connection_options['use_text']) ? (boolean) $connection_options['use_text'] : ODBC_USE_TEXT;

    // The encoding to use for higher characters
    $this->encoding = isset($connection_options['encoding']) ? $connection_options['encoding'] : ODBC_ENCODING;
    // if ($this->encoding == 'u' && !ODBC_CAN_ENCODE_UNICODE)
    //   $this->encoding = 'html';

    // Build the DSN.
    $options = array(
      // TODO: Make this work with a named DSN
      'DRIVER=' . $connection_options['odbc_driver'],
      // TODO: Check whether we pass port information in the same way as sqlsrv
      'Server=' . $connection_options['host'] . (!empty($connection_options['port']) ? ',' . $connection_options['port'] : ''),
      'Database=' . $connection_options['database'],
    );

    if ($connection_options['odbc_driver'] == 'FreeTDS' && isset($connection_options['tds_version'])) {
      $options[] = 'TDS_Version='.$connection_options['tds_version'];
    }

    // Launch the connection to the server.
    parent::__construct('odbc:' . implode(';', $options), $connection_options['username'], $connection_options['password'], array(
      // We ask PDO to perform the placeholders replacement itself because
      // SQL Server is not able to detect duplicated placeholders in
      // complex statements.
      // E.g. This query is going to fail because SQL Server cannot
      // detect that length1 and length2 are equals.
      // SELECT SUBSTRING(title, 1, :length1)
      // FROM node
      // GROUP BY SUBSTRING(title, 1, :length2);
      // This is only going to work in PDO 3 but doesn't hurt in PDO 2.
      PDO::ATTR_EMULATE_PREPARES => TRUE,
    ));

    $this->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    // Fetch the name of the user-bound schema. It is the schema that SQL Server
    // will use for non-qualified tables.
    $this->schema()->defaultSchema = $this->query("SELECT SCHEMA_NAME()")->fetchField();
  }

  /**
   * Override of PDO::prepare(): prepare a prefetching database statement.
   *
   * @status tested
   */
  public function prepare($query, $options = array()) {
    $query = $this->preprocessQuery($query);
    return new DatabaseStatement_odbc($this, $query, $options);
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
    if (!$this->bypassQueryPreprocess) {
      $query = $this->preprocessQuery($query);
    }
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
    # Everything that follows a boundary that is not : or _.
    \b(?<![:\[_])(?:
      # Any reserved words, followed by a boundary that is not an opening parenthesis.
      (action|admin|alias|any|are|array|at|begin|boolean|class|commit|contains|current|data|date|day|depth|domain|external|file|full|function|get|go|host|input|language|last|less|local|map|min|module|new|no|object|old|open|operation|parameter|parameters|path|plan|prefix|proc|public|ref|result|returns|role|row|rows|rule|save|search|second|section|session|size|state|statistics|temporary|than|time|timestamp|tran|translate|translation|trim|user|value|variable|view|without)
      (?!\()
      |
      # Or a normal word.
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

  public function quote($str, $paramType = PDO::PARAM_STR) {
    if (is_null($str)) return '';
    if (is_array($str)) {
      $this->watchdog('Attempt to use array as parameter: %arr', array('%arr' => $str));
      return "''";
    }

    // $quoted = parent::quote($str, $paramType);

    $quoted = $str;
    $quoted = $this->encode($quoted);
    // $quoted = str_replace(':', '\:', $quoted);
    // $quoted = str_replace('?', '\?', $quoted);
    $quoted = str_replace("'", "''", $quoted);
    $quoted = "'".$quoted."'";

    return $quoted;
  }

  public function encode($str) {
    if (!is_string($str))
      return $str;

    switch ($this->encoding) {
      case 'html':
        $encoded = htmlentities($str, ENT_NOQUOTES | ENT_IGNORE | ENT_HTML401, 'UTF-8', false);
        return $encoded;

      case 'u':
        $encoded = unicode_x_encode($str);
        return $encoded;

      case 'url':
        $encoded = urlencode($str);
        return $encoded;

      case 'quoted-printable':
        $encoded = quoted_printable_encode($str);
        //  correct newlines back, we have no real need to split them
        $encoded = str_replace("=\r\n", "", $encoded);
        return $encoded;

      default:
        return $str;
    }
  }

  public function decode($str) {
    if (!is_string($str))
      return $str;

    try {
      // look for quoted-printable
      // if (preg_match('/=[0-9A-F]{2}/', $str)) {
      //   $decoded = quoted_printable_decode($str);
      //   if (!is_string($decoded))
      //     return $str;
      //   if (empty($decoded) && !empty($str))
      //     return $str;

      //   // special case to make sure we're not breaking serialised data
      //   if (preg_match('/(a:[0-9]+:{|s:[0-9]+:")/', $str)) {
      //     $unserialized = unserialize($decoded);
      //     if ($unserialized == FALSE) {
      //       $unserialized = unserialize($str);
      //       if ($unserialized !== FALSE)
      //         return $str;
      //     }
      //   }

      //   return $decoded;
      // }

      // look for unicode escaping
      // if (preg_match('/\\u[0-9A-F]{4}/', $str)) {
      //   $decoded = unicode_decode($str);
      //   return $str;
      // }

      return unicode_x_decode($str);

      // look for URL encoding
      // if (preg_match('/%[0-9A-F]{2}/', $str)) {
      //   $decoded = urldecode($str);
      //   return $str;
      // }

      // decode HTML entities
      // $decoded = html_entity_decode($str, ENT_NOQUOTES | ENT_HTML401, 'UTF-8');

      // // special case to make sure we're not breaking serialised data
      // if (preg_match('/(a:[0-9]+:{|s:[0-9]+:")/', $str)) {
      //   $unserialized = unserialize($decoded);
      //   if ($unserialized == FALSE) {
      //     $unserialized = unserialize($str);
      //     if ($unserialized !== FALSE)
      //       return $str;
      //   }
      // }
      // return $decoded;

      // return $str;

    } catch (Exception $e) {
      $this->watchdog('Error decoding string: "%str"', array('%str' => $str));
      return $str;
    }
  }

  public function quoteIdentifier($identifier) {
    return '[' . $identifier .']';
  }

  public function escapeField($field) {
    if (strlen($field) > 0) {
      return implode('.', array_map(array($this, 'quoteIdentifier'), explode('.', preg_replace('/[^A-Za-z0-9_.]+/', '', $field))));
    }
    else {
      return '';
    }
  }

  public function quoteIdentifiers($identifiers) {
    return array_map(array($this, 'quoteIdentifier'), $identifiers);;
  }

  /**
   * Override of DatabaseConnection::query().
   */
  public function query($query, array $args = array(), $options = array()) {
    try {
      if (is_string($query)) {
        if (ODBC_WATCHDOG_DEBUG) { echo "<p>Query: prepreparing \"<tt>$query</tt>\" with arguments:</p>\n<pre>"; print_r($args); echo "</pre>\n"; }
        $sql = $this->replacePlaceholders($query, $args);
        if (ODBC_WATCHDOG_DEBUG) { echo "<p>Query: preprepared \"<tt>$sql</tt>\"</p>\n"; }
        return parent::query($sql, array(), $options);
      } else {
        return parent::query($query, $args, $options);
      }
    } catch (Exception $e) {
      $this->watchdog(get_class($e).' when executing "%sql" with variables %vars: '.$e->getMessage(), array('%sql' => (string) $query, '%vars' => $args), WATCHDOG_ERROR);
      throw $e;
    }
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
    // Generate a new temporary table name and protect it from prefixing.
    // SQL Server requires that temporary tables to be non-qualified.
    $tablename = '#' . $this->generateTemporaryTableName();
    $prefixes = $this->prefixes;
    $prefixes[$tablename] = '';
    $this->setPrefix($prefixes);

    // Replace SELECT xxx FROM table by SELECT xxx INTO #table FROM table.
    $query = preg_replace('/^SELECT(.*?)FROM/i', 'SELECT$1 INTO ' . $tablename . ' FROM', $query);

    $this->query($query, $args, $options);
    return $tablename;
  }

  /**
   * Internal function: do a manual placeholder replacement for a query.
   * New version to build into a buffer rather than using str_replace, to avoid falling foul of
   */
  public function replacePlaceholders($query, array $args = array()) {
    if (empty($args))
      return $query;

    // make sure the values are all formatted for SQL
    $args = array_map(array($this, 'inlineValue'), $args);

    //  locate the arguments in the input string
    $placeholders = array();
    if (range(0, count($args) - 1) === array_keys($args)) {
      //  using ordered arguments,  ?
      //  we can be sure the arguments are in order
      $offset = 0;
      $currentArg = 0;
      while ($pos = strpos($query, '?', $offset) && $currentArg < count($args)) {
        $placeholders[] = array($pos, '?', $args[$currentArg]);
        $currentArg++;
        $offset = $pos + 1;
      }
    } else {
      //  using named arguments,  :name
      //  these aren't necessarily in order in SQL, so get positions for all the ones we can find
      foreach ($args as $key => $value) {
        if ($key[0] != ':') {
          $key = ":$key";
        }
        $offset = 0;
        while ($pos = strpos($query, $key, $offset)) {
          $placeholders[$pos] = array($pos, $key, $value);
          $offset = $pos + 1;
        }
      }
      // sort the indices by their position
      ksort($placeholders);
      $placeholders = array_values($placeholders);
    }
    // if (ODBC_WATCHDOG_DEBUG) {
    //   echo "<p>Replacing placeholders in \"<tt>$query</tt>\":</p>\n<pre>";
    //   print_r($placeholders);
    //   echo "</pre>\n";
    // }


    // build the output buffer
    $buf = "";
    $offset = 0;
    foreach ($placeholders as list($index, $key, $value)) {
      if (ODBC_WATCHDOG_DEBUG && substr($query, $index, strlen($key)) != $key) echo "<p><b>Error! Mismatching placeholder at offset $index: \"".substr($query, $index, strlen($key))."\" != \"$key\"</p>\n";
      $buf .= substr($query, $offset, $index - $offset);
      $buf .= $value;
      $offset = $index + strlen($key);
    }
    $buf .= substr($query, $offset);

    // if (ODBC_WATCHDOG_DEBUG) echo "<p>Result: \"<tt>$buf</tt>\":</p>\n";
    
    return $buf;
  }

  function inlineValue($value) {
    // if (is_array($value) && count($value) == 1 && isset($value[0]))
    //   $value = $value[0];
    if (is_array($value)) {
      $values = array_map(array($this, 'inlineValue'), $value);
      return implode(', ', $values);
    }

    if (is_null($value))
      return 'null';
    if (is_float($value))
      return sprintf('%F', $value);
    if (is_int($value))
      return sprintf('%d', $value);
    return $this->quote($value);
  }

  /*
  public function replacePlaceholders($query, array $args = array()) {
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
  }*/

  /**
   * Internal function: massage a query to make it compliant with SQL Server.
   */
  public function preprocessQuery($query) {
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
      // Run a non-operaiton query to avoid a fatal error
      // when no query is runned.
      '/^RELEASE SAVEPOINT (.*)$/' => 'SELECT 1 /* $0 */',
    );
    $query = preg_replace(array_keys($replacements), $replacements, $query);

    // Add prefixes to Drupal-specific functions.
    $functions = array(
      'SUBSTRING',
      'SUBSTRING_INDEX',
      'GREATEST',
      'CONCAT',
      'IF',
    );
    foreach ($functions as $function) {
      $query = preg_replace('/\b(?<![:.])(' . preg_quote($function) . ')\(/i', $this->schema()->defaultSchema . '.$1(', $query);
    }

    $replacements = array(
      'LENGTH' => 'LEN',
      'POW' => 'POWER',
    );
    foreach ($replacements as $function => $replacement) {
      $query = preg_replace('/\b(?<![:.])(' . preg_quote($function) . ')\(/i', $replacement . '(', $query);
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
          SELECT sub2.*, ROW_NUMBER() OVER(ORDER BY sub2.__line2) AS __line3 FROM (
            SELECT 1 AS __line2, sub1.* FROM (' . $query . ') AS sub1
          ) as sub2
        ) AS sub3
        WHERE __line3 BETWEEN ' . ($from + 1) . ' AND ' . ($from + $count);
    }

    return $query;
  }

  public function mapConditionOperator($operator) {
    // SQL Server doesn't need special escaping for the \ character in a string
    // literal, because it uses '' to escape the single quote, not \'. Sadly
    // PDO doesn't know that and interpret \' as an escaping character. We
    // use a function call here to be safe.
    static $specials = array(
      'LIKE' => array('postfix' => " ESCAPE CHAR(92)"),
      'NOT LIKE' => array('postfix' => " ESCAPE CHAR(92)"),
    );
    return isset($specials[$operator]) ? $specials[$operator] : NULL;
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

  public function lastInsertId($name = null) {
    $last = $this->query('SELECT @@IDENTITY')->fetchField();
    // $this->watchdog('Last insert ID: %id', array('%id' => $last));
    return $last;
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

class DatabaseStatement_odbc extends DatabaseStatementPrefetch implements Iterator, DatabaseStatementInterface {
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
    if (ODBC_EMULATE_PREPARES) {
      $queryString = $this->dbh->prefixTables($this->queryString);
      $queryString = $this->dbh->replacePlaceholders($queryString, $args);

      $statement = $this->getStatement($queryString);
      if (!$statement) {
        $this->dbh->watchdog('No statement for query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
        $this->throwPDOException();
      }
      $return = $statement->execute();
      if (!$return) {
        $this->dbh->watchdog('Failed to execute query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
        $this->throwPDOException();
      }

    } else {
      $statement = $this->getStatement($this->queryString, $args);
      if (!$statement) {
        $this->dbh->watchdog('No statement for query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
        $this->throwPDOException();
      }

      $return = $statement->execute($args);
      if (!$return) {
        $this->dbh->watchdog('Failed to execute query "%sql"', array('%sql' => $this->queryString), WATCHDOG_ERROR);
        $this->throwPDOException();
      }
    }

    // Fetch all the data from the reply, in order to release any lock
    // as soon as possible.
    $this->rowCount = $statement->rowCount();

    try {
      $this->data = $statement->fetchAll(PDO::FETCH_ASSOC);

      //  decode strings if necessary
      if ($this->dbh->encoding != '') $this->decodeData();
    }
    catch (Exception $e) {
      $this->dbh->watchdog(get_class($e).' when executing "%sql": '.$e->getMessage(), array('%sql' => $this->queryString), WATCHDOG_ERROR);
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

    // Remove technical columns from the final result set.
    // TODO: Do we want to rename these options?
    $droppable_columns = array_flip(isset($options['sqlsrv_drop_columns']) ? $options['sqlsrv_drop_columns'] : array());
    $dropped_columns = array();
    foreach ($this->columnNames as $k => $column) {
      if (substr($column, 0, 2) == '__' || isset($droppable_columns[$column])) {
        $dropped_columns[] = $column;
        unset($this->columnNames[$k]);
      }
    }

    if ($dropped_columns) {
      // Renumber columns.
      $this->columnNames = array_values($this->columnNames);

      foreach ($this->data as $k => $row) {
        foreach ($dropped_columns as $column) {
          unset($this->data[$k][$column]);
        }
      }
    }

    // Destroy the statement as soon as possible.
    unset($statement);

    // Initialize the first row in $this->currentRow.
    $this->next();

    return $return;
  }

  public function decodeData() {
    if (!is_array($this->data))
      return;
    if (count($this->data) > 0 && isset($this->data[0]) && !is_array($this->data[0]))
      return;

    $decoded_rows = array();
    foreach ($this->data as $row) {
      $decoded_row = array();
      foreach ($row as $key => $value) {
        $decoded_value = $this->dbh->decode($value);
        $decoded_row[$key] = $decoded_value;
      }
      $decoded_rows[] = $decoded_row;
    }

    // if ($decoded_rows !== $this->data) {
    //   echo "<pre>Decoding ";
    //   print_r($this->data);
    //   echo " -> ";
    //   print_r($decoded_rows);
    //   echo "</pre>\n";
    // }
    $this->data = $decoded_rows;
  }
}

/**
 * @} End of "ingroup database".
 */

/**
 * Workaround a bug in the database autoloader by defining all the child classes.
 *
 * @see http://drupal.org/node/851136
 */
class DatabaseTransaction_odbc extends DatabaseTransaction {
  // public function __construct(DatabaseConnection $connection, $name = NULL) {
  //   parent::__construct($connection, $name);
  //   if (ODBC_WATCHDOG_DEBUG) echo "<p>Starting transaction: ".$connection->transactionDepth()."</p>";
  // }

  // public function __destruct() {
  //   parent::__destruct();
  //   if (ODBC_WATCHDOG_DEBUG) echo "<p>Ending transaction: ".$this->connection->transactionDepth()."</p>";
  // }
}


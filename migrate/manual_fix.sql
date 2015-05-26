update variable set value='s:6:"Next >";' where name='colorbox_text_next';
update variable set value='s:6:"< Prev";' where name='colorbox_text_previous';


--update variable set value='s:6:"Next »";' where name='colorbox_text_next';
--update variable set value='s:6:"« Prev";' where name='colorbox_text_previous';


-- UNIX_TIMESTAMP()
exec('DROP FUNCTION UNIX_TIMESTAMP');

exec('
CREATE FUNCTION UNIX_TIMESTAMP (
@ctimestamp datetime
)
RETURNS integer
AS 
BEGIN
  /* Function body */
  declare @return integer

  SELECT @return = DATEDIFF(SECOND,{d ''1970-01-01''}, @ctimestamp)

  return @return
END
');




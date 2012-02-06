drop procedure if exists sp_vend_success;

/*
  Vending machine has confirmed the vend was succesfull. Update vend_log with the datetime & item position,
  update the transactions table to show it's complete, and update the members balance.

*/

DELIMITER //
CREATE PROCEDURE sp_vend_success
(
   IN  rfid_serial  varchar( 50),
   IN  vend_tran_id varchar(  6),
   IN  pos          varchar( 10),
   OUT err          varchar(100)
)
SQL SECURITY DEFINER
BEGIN
  declare ck_exists int;
  declare tran_id   int;
  declare vdesc     varchar(100);

  set err = '';

  main: begin  

    -- Check the transaction id & serial id match up with an active vend entry
    select count(*) into ck_exists
    from vend_log v  
    where v.vend_tran_id = vend_tran_id
      and v.rfid_serial = rfid_serial 
      and v.cancelled_datetime is null
      and v.transaction_id is not null
      and v.position is null;
      
    if (ck_exists = 0) then
      set err = 'unable to find matching entry in vend_log (BUG)'; 
      leave main;
    end if;

    select 
      v.transaction_id
    into
      tran_id
    from vend_log v 
    where v.vend_tran_id = vend_tran_id;

    set vdesc = concat('Vend complete, position: ', pos);
    
    call sp_transaction_update(tran_id, 'COMPLETE', vdesc, err);
    if (length(err) > 0) then
      leave main;
    end if;
    
    update vend_log
    set success_datetime = sysdate(), position = pos
    where vend_log.vend_tran_id = vend_tran_id;    

  end main;
 

END //
DELIMITER ;


GRANT EXECUTE ON PROCEDURE sp_vend_success TO 'gk'@'localhost'
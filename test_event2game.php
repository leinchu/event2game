<?php
error_reporting(E_ALL);
ini_set('display_errors', true);

if(!function_exists('evtgame_send'))
{
	exit('Event2game is not exists!');
}

$connections = array();

function open($rsid, $fd){
	global $connections;
	$connections[$fd] = $rsid;
	var_dump($rsid, $fd);
	echo __FILE__.' ( '.__LINE__.' ) open : '.$fd.chr(10);
}

function close($fd){
	global $connections;
	unset($connections[$fd]);
	echo __FILE__.' ( '.__LINE__.' ) close : '.$fd.chr(10);
}

function recv($fd, $str){
	global $connections;
	echo __FILE__.' ( '.__LINE__.' ) '.chr(10);
	echo $fd .' say '.$str.chr(10);	

	evtgame_send($connections[$fd], 'You say: '.$str);
}

function storage_job($id)
{
	echo __FILE__.' ( '.__LINE__.' ) '.chr(10);
	echo 'Storage_job start, my no. is: '.$id.chr(10);
	//doing something, you must careful of using global variables, or you will got segment error!
}

$rs = evtgame_set_function("open","close","recv");

if($rs)
{
	echo "Set function: OK\n";
	evtgame_run();
}else
	echo "Set function: Failed\n";

echo "---------------------------------\n";
$rs = evtgame_thread_start('storage_job');

//Give time for socket server to runging;
//Try: telnet 127.0.0.1 8080
$i=30;
while(--$i)
{
	sleep(3);//runs 30 * 3 = 90 senconds
}
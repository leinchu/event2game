<?php
error_reporting(E_ALL);
ini_set('display_errors', true);
echo time().' time ' ;
if(isset($_SERVER['session_id']))
{
	var_dump($_SERVER);
}
else
{
	evtgame_cgi_filepath('/www/hualike/cgi.php');
	echo evtgame_cgi_request(md5(rand()), "Hello!", '127.0.0.1', 9000);
}
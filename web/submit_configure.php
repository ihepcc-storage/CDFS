<?php

header("Content-Type: text/html; charset=UTF-8");

$tag=0;
if(!empty($_POST["ip"]))
{
	echo $_POST["ip"] . "<br>";
}
else{
	echo "No Remote Server IP Set";
	$tag=1;
}
if(!empty($_POST["consol"]))
{
	echo $_POST["consol"] . "<br>";
}
else{
	echo "No Communicate Port Set" . "<br>";
	$tag=1;
}
if(!empty($_POST["ddir"]))
{
	echo $_POST["ddir"] . "<br>";
}
else{
	echo "No Data Source DIR Set" . "<br>";
	$tag=1;
}
if(!empty($_POST{"bdir"}))
{
	echo $_POST["bdir"] . "<br>";
}else{
	echo "NO Cache File DIR Set" . "<br>";
	$tag=1;
}
if($tag==0){
	$confile=fopen("cdfs.cf", "w") or die("Unable tp open file!");
	$content="[System Configure]"."\n";
	fwrite($confile, $content);
	$content="DATA_SERVER_IP=".$_POST["ip"]."\n";
	fwrite($confile, $content);
	$content="DATA_SERVER_CONSOL=".$_POST["consol"]."\n";
	fwrite($confile, $content);
	$contnet="DATA_DIR=".$_POST["ddir"]."\n";
	fwrite($confile, $content);
	$content="BLOCK_DIR=".$_POST["bdir"]."\n";
	fwrite($confile, $content);
	fclose($confile);
	$rmcmd="sudo rm -f /etc/cdfs/cdfs.cf.web";
	$cpcmd="sudo cp cdfs.cf /etc/cdfs/cdfs.cf.web";
	exec($rmcmd);
	exec($cpcmd);
}
?>
<html>
<head>
<meta http-equiv="refresh" content="1;url=http://bigdata07.ihep.ac.cn/cdfs/">     
</head>
</html>

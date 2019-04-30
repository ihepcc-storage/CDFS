<?php
header("Content-Type: text/html; charset= UTF-8;");
function deal(){
	if(empty($_POST["pfrom_path"])){
		echo "File you want to put is not set"."<br>";
		exit();
	}
	if(empty($_POST["pto_path"])){
		echo "Transfer target DIR is not set"."<br>";
		exit();
	}
	$cmd="sudo iput -v ".$_POST["pfrom_path"]." ".$_POST["pto_path"]." 2>&1";
	echo "<h3>$cmd</h3>";
	exec($cmd, $array, $res1);
	if(empty($array) and $res1==1){
		echo "iput failed, please check path!". "<br>";
	}else if(empty($array) and $res1==0){
		echo "Put success"."<br>";
	}
	else{
		echo "<h4>Put Info:</h4>";
		foreach($array as $value){
			echo $value. "<br/>";
		}
	}
}
$url="http://bigdata07.ihep.ac.cn/cdfs/transfer.html";	

?>

<html>
<head>
<meta http-equiv="refresh" content="180;url=<?php echo $url; ?>">
<style>
        .homelink{
                text-decoration: none;
                color: #000;
        }
</style>
</head>
<body>
<a class="homelink" href="http://bigdata07.ihep.ac.cn/cdfs">
<h1 style="text-align:center">Put Data Block to Data Source DIR of the Remote Station</h1>
<a>
<span> <?php deal(); ?></span>
<br>
<a href="<?php echo $url; ?>">
<button> Go Back</button>
</a>
</body>
</html>

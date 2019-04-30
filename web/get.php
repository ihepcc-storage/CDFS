<?php
header("Content-Type: text/html; charset= UTF-8;");
function deal(){
	if(empty($_POST["gfrom_path"])){
		echo "File you want to get is not set"."<br>";
		exit();
	}
	if(empty($_POST["gto_path"])){
		echo "Transfer target DIR is not set"."<br>";
		exit();
	}
	$cmd="sudo /usr/bin/iget -fmv ".$_POST["gfrom_path"]." ".$_POST["gto_path"]." 2>&1";
	echo "<h3>$cmd</h3>";
	exec($cmd, $array, $res1);
	if(empty($array)){
		echo "iget failed, please check path!". "<br>";
	}else{
		echo "<h4>Get Info:</h4>";
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
<h1 style="text-align:center">Get Data Block from Data Source DIR of the Remote Station</h1>
<a>
<span> <?php deal(); ?></span>
<br>
<a href="<?php echo $url; ?>">
<button> Go Back</button>
</a>
</body>
</html>

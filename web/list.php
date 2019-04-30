<?php
header("Content-Type: text/html; charset=UTF-8; ");
function deal(){
	if(empty($_POST["path"])){
		echo "the Target Path is not set". "<br>";
		exit();
	}else{
		$cmd="sudo ilist -l ".$_POST["path"]. " 2>&1";
		echo "<h3>$cmd</h3>";
	}
	exec($cmd, $array, $res);
	if(empty($array) and $res==1){
		echo "List failed, please check the path!"."<br>";
	}else if(empty($array) and $res==0){
		echo $_POST["path"].": No subfiles exist"."<br>";
	}
	else{
		echo "total(files):   ".count($array)."<br>";
		foreach($array as $value){
			echo $value. "<br/>";
		}
	}
}
$url="http://bigdata07.ihep.ac.cn/cdfs/list.html"
?>

<html>
<head>
<meta http-equiv="refresh" content="180;url=<?php echo $url; ?>">     
</head>
<style>
	.homelink{
		text-decoration: none;
		color: #000;
	}
</style>
<body>
<a class="homelink" href="http://bigdata07.ihep.ac.cn/cdfs">
<h1 style="text-align:center">List Files of Data Source DIR on the Remote Station</h1>
<a>
<span> <?php deal(); ?></span>
<br>
<a href="<?php echo $url; ?>">
<button> Go Back</button>
</a>
</body>     
</html>  

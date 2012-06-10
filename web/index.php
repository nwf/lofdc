<html>

<!-- ********* STYLESHEET LINK ********* -->
	
	<head>

		<LINK REL=StyleSheet HREF="styles.css" TYPE="text/css" MEDIA=screen>
		<title> The Node: Door </title> 
	
	</head>


<!-- ********* UNIVERSAL CONTENT ********* -->
	
	
	<body>
					
		<div id="centerDiv"> 
		
				
			<div id="logo">
				<img src="media/logo.png" border="0" alt="//media"></a>					
			</div>
				
				
			<div id="webcambox">
				<div id="webcam">
					<img src="media/cat.png" border="0" alt="//media"></a>				
				</div>								
			</div>
				
				
			<div id="loginbox">
				<div id="logintext">
					<h1> Login </h1>	
				</div>	
			</div>
				
				
			<div id="unlockbox">
				<div id="unlocktext">
					<h1> Unlock </h1>	
				</div>	
			</div>				
				
	
<!-- ********* ACTIVITY CONTENT ********* -->

				
			<div id="activity">
			
			
				<div id="usersheader">
					<h3> Recent Users - Admin - E-Mail</h3>
				</div>
				
				
				<div id="users">
					<?php
						$db=new PDO('sqlite:./media/door.db');
						$sql = "SELECT * FROM users";
						foreach ($db->query($sql)as $row)
					{
						echo "User ";
						print $row['user_id'] . ' - ' . $row['admin']  . ' - ' . $row['email'] . '<br/>';
					}
						echo "John Smith - 0 - icouldbeanyone@email.com<br/>";
						echo "Peter Johnson - 0 - jeebuslong@email.com <br/>";
						echo "Mary JoHanson - 0 - mynameismary@email.com <br/>";
					?>
				</div>
			
			
				<div id="timestamp">
					<?php
						$db=new PDO('sqlite:./media/door.db');
						$sql = "SELECT * FROM log";
						foreach ($db->query($sql)as $row)
					{
						print $row['message'] . '<br/>';
					}
						echo "4pm <br/>";
			
					?>
				</div>
				
				
				<div id="timestampheader">
					<h3> Time In</h3>
				</div>
				
				
		</div>
				
				
			<div id="activitytext">
				<h2> Recent Activity </h2>
			</div>



	</div>
	</body>
</html>



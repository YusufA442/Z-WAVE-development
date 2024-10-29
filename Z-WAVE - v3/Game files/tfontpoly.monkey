Import mojo

Class TFont_Poly
	Field Clockwise:Bool
	Field Scaler:Float
	Field Triangles:Stack<Float[] > = New Stack<Float[] >
	Field xyList:Float[]
	Field OrigArray:Float[]
	Global points:FloatStack = New FloatStack
	Field UsedArr:Bool[]
	
	Method Draw()
		#If TARGET <> "html5" And TARGET <> "flash" Then
			For Local i:Int = 0 Until Triangles.Length
				If Clockwise Then
					DrawPoly(Triangles.Get(i))
				Else
					DrawPoly(Triangles.Get(Triangles.Length - i - 1))
				End
			Next
		#Else
			DrawPoly(OrigArray)
		#End
	End Method

	Method New(xyList:Float[], Scaler:Float)
		'Setup
		Self.Scaler = Scaler
		Self.OrigArray = xyList
		Self.xyList = xyList
		
		'Get Clockwise
		Self.Clockwise = GetClockWise()
		#If TARGET <> "html5" And TARGET <> "flash" Then
			BuildPoints()
			Triangulate()
		#End
	End Method

	Method BuildPoints:Void()
		points.Clear()
		For Local i = 0 Until xyList.Length Step 2
			Local idx:Int = i
			If Not Clockwise Then idx = xyList.Length - i - 2
			Local found:Bool = False
			Local x = xyList[idx]
			Local y = xyList[idx + 1]
			For Local j = 0 Until points.Length Step 2
				If Int(x * Scaler) = Int(points.Get(j) * Scaler) And Int(y * Scaler) = Int(points.Get(j + 1) * Scaler) Then
					found = True
					Exit
				End
			Next
			If Not found Then
				points.Push(x)
				points.Push(y)
			End If
		Next
	End Method
	Method GetClockWise:Bool()
		Local Total
		For Local p1 = 0 Until xyList.Length/2
			Local p2 = p1 + 1; If p2 = xyList.Length/2 Then p2 = 0
			Total += ((xyList[p2*2] - xyList[p1*2]) * (xyList[p2*2+1] + xyList[p1*2+1]))
		Next
		If Total < 0 Then Return True
		Return False
	End Method
	Method DotProduct:Float(p0, p1, p2)
		Local vx0 = points.Get(p0*2)
		Local vy0 = points.Get(p0*2+1)
		Local vx2 = points.Get(p2*2)
		Local vy2 = points.Get(p2*2+1)
		Local vx1 = points.Get(p1*2)
		Local vy1 = points.Get(p1*2+1)
		Return (vx1 - vx0) * (vy2 - vy1) - (vx2 - vx1) * (vy1 - vy0)
	End Method
	Method Angle:Float(p1)
		Local p0 = p1 - 1; If p0 = -1 Then p0 = points.Length/2 - 1
		Local p2 = p1 + 1; If p2 = points.Length/2 Then p2 = 0
		Local head0:Float = Heading(points.Get(p0*2), points.Get(p0*2+1), points.Get(p1*2), points.Get(p1*2+1))
		Local head1:Float = Heading(points.Get(p1*2), points.Get(p1*2+1), points.Get(p2*2), points.Get(p2*2+1))
		Local delta:Float = head1 - head0
		While delta > 180
			delta -= 360
		Wend
		While delta < - 180
			delta += 360
		Wend
		Return delta
	End Method
	Method Heading:Float(x0:Float, y0:Float, x1:Float, y1:Float)
		Return ATan2(y1 - y0, x1 - x0)
	End Method
	Method Triangulate()
		'If True Then Return
		While points.Length > 4
			'ExitFlag
			Local ExitFlag = 1
			For Local i = 0 Until points.Length/2
				Local Ang:Float = Angle(i)
				If Ang < 180 And Ang > 0 Then ExitFlag = 0; Exit
			Next
			If ExitFlag = 1 Then Exit
			
			Local Found = 0
			For Local p1 = 0 Until points.Length/2
				Local Ang:Float = Angle(p1)
				If Ang < 180 And Ang > 0 Then
					Local p0 = p1 - 1; If p0 = -1 Then p0 = points.Length/2 - 1
					Local p2 = p1 + 1; If p2 = points.Length/2 Then p2 = 0
					If Not AnyInside(p0, p1, p2) Then
						Triangles.Push([points.Get(p0*2), points.Get(p0*2+1),points.Get(p1*2), points.Get(p1*2+1),points.Get(p2*2), points.Get(p2*2+1) ])
						points.Remove(p1*2)
						points.Remove(p1*2)
						Found = 1
						Exit
					End If
				End If
			Next
			
			If Found = 0 Then Exit
			
		End
	End Method
	Method AnyInside:Bool(p0, p1, p2)
		For Local i = 0 Until points.Length/2
			If i <> p0 And i <> p1 And i <> p2 Then
				If DotProduct(p0, p1, i) > 0 Then
					If DotProduct(p1, p2, i) >= 0 Then
						If DotProduct(p2, p0, i) > 0 Then
							Return True
						EndIf
					EndIf
				EndIf	
			End If
		Next
		Return False
	End Method
End Class
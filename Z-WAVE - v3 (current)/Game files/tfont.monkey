#TEXT_FILES+="*.ttf"
Import mojo
Import tfontdatastream
Import tfontpoly



Class TFont
	Field Stream:DataStream
	Field Size
	Field Color:Int[]
	Field Path:String
	
	Field OutlineType:String ' "TT" or "OT"
	Field GlyphNumber
	Field FontLimits[4]
	Field FontScale:Float
	Field LineHeight

	Field GlyphId:Int[]
	Field Glyph:TFont_Glyph[]
	Field ImagesLoaded:Bool = False
	Field FontClockwise:Bool
	Field ClockwiseFound:Bool = False
	
	
	'Load a new Font
	Method New(Path:String, Size, Color:Int[] =[0, 0, 0])
		Stream = New DataStream("monkey://data/" + Path, True)
		Self.Size = Size
		Self.Color = Color
		Self.Path = Path
		If Stream.Buffer = Null Then Error Path + " : Font file not found"
		
		'===== Read Offset Table =====
		Local sfntVersion = Stream.ReadFixed32()
		Local numTables = Stream.ReadUInt(2)
		Local searchRange = Stream.ReadUInt(2)
		Local entrySelector = Stream.ReadUInt(2)
		Local rangeShift = Stream.ReadUInt(2)
		If Int(sfntVersion) = 1 Then
			OutlineType = "TT"
		Else
			OutlineType = "OT"
		End If
		
		'===== Load Offset Table Records and get offsets ===== 
		Local cmapOffset, headOffset, hheaOffset, hmtxOffset, maxpOffset, nameOffset, glyfOffset, locaOffset, CFFOffset, VORGOffset
		For Local i = 0 To numTables - 1
			Local tag:String = Stream.ReadString(4)
			Local checksum = Stream.ReadUInt(4)
			Local offset = Stream.ReadUInt(4)
			Local length = Stream.ReadUInt(4)
			Select tag
				Case "cmap"
					cmapOffset = offset
				Case "head"
					headOffset = offset
				Case "hhea"
					hheaOffset = offset
				Case "hmtx"
					hmtxOffset = offset
				Case "maxp"
					maxpOffset = offset
				Case "name"
					nameOffset = offset
				Case "glyf"
					glyfOffset = offset
				Case "loca"
					locaOffset = offset
				Case "CFF "
					CFFOffset = offset
				Case "VORG"
					VORGOffset = offset
			End Select
		Next
		
		
		'===== Peek some font data =====
		GlyphNumber = Stream.PeekUInt(2, maxpOffset + 4)
		FontLimits[0] = Stream.PeekInt(2, headOffset + 36) 'xMin
		FontLimits[1] = Stream.PeekInt(2, headOffset + 38) 'yMin
		FontLimits[2] = Stream.PeekInt(2, headOffset + 40) 'xMax
		FontLimits[3] = Stream.PeekInt(2, headOffset + 42) 'yMax
		FontScale = (Size * 1.0) / (FontLimits[3])' - FontLimits[1])
		LineHeight = Size
		Local LocaFormat = Stream.PeekInt(2, headOffset + 50)
		Local HMetricsNumber = Stream.PeekUInt(2, hheaOffset + 34)

		'===== Setup Glyph Arrays =====
		GlyphId = New Int[1000]
		Glyph = New TFont_Glyph[GlyphNumber]
		For Local i = 0 To GlyphNumber - 1
			Glyph[i] = New TFont_Glyph
		Next
		
		'===== Load Big Data =====
		LoadMetrics(hmtxOffset, HMetricsNumber)
		LoadCmapData(cmapOffset)
		LoadLoca(locaOffset, LocaFormat, glyfOffset)
		
		'Load glyph data
		For Local g = 0 To GlyphNumber - 1
			'Load Points
			LoadGlyfData(g, Glyph[g].FileAddress)
			QuadGlyph(g)
			SmoothGlyph(g)
		Next
		
		'Make Poly
		For Local g = 0 To GlyphNumber - 1
			Glyph[g].Poly = New TFont_Poly[Glyph[g].ContourNumber]
			For Local c = 0 To Glyph[g].ContourNumber - 1
				Glyph[g].Poly[c] = New TFont_Poly(Glyph[g].xyList[c], FontScale)
				If Glyph[g].ContourNumber = 1 And Not ClockwiseFound Then
					FontClockwise = Glyph[g].Poly[0].Clockwise
					ClockwiseFound = True
				EndIf
			Next
		Next
		
		
	End Method
	
	
	Method DrawText(Text:String, x, y, CenterX = 0, CenterY = 0, AdditionalLetterSpace = 0, AdditionalLineSpace = 0)
		'Load Font
		If ImagesLoaded = False Then
			LoadGlyphImages()
			ImagesLoaded = True
		End If
		
		'Draw Lines of Text
		Local X = x
		Local Y = y
		
		If CenterY = 1 Then
			Y = Y - Self.TextHeight(Text, AdditionalLineSpace) / 2
		End If
		
		For Local L:String = EachIn Text.Split("~n")
			For Local c = EachIn L
				Local Id = GlyphId[c]
				Local tx = X + Glyph[Id].xMin * FontScale
				Local ty = Y - (Glyph[Id].yMax * FontScale) + LineHeight
				
				If CenterX = 1 Then
					tx = tx - (Self.TextWidth(L, AdditionalLetterSpace)) / 2
				End If
				
				If c > 32 Then
					If Glyph[Id].Img <> Null Then DrawImage(Glyph[Id].Img, tx, ty)
				End If
				X = X + Glyph[Id].Adv * FontScale + AdditionalLetterSpace
			Next
			
			X = x
			Y = Y + LineHeight + AdditionalLineSpace
		Next
		
		
		
	End Method
		
	Method TextWidth(Text:String, AdditionalLetterSpace = 0)
		Local Width = 0
		For Local L:String = EachIn Text.Split("~n")
			Local TempWidth = 0
			For Local c = EachIn L
				Local Id = GlyphId[c]
				TempWidth = TempWidth + (Glyph[Id].Adv * FontScale) + AdditionalLetterSpace
			Next
			If TempWidth > Width Then Width = TempWidth
		Next
		
		Return Width
	End Method
	
	Method TextHeight(Text:String, AdditionalLineSpace = 0)
		Local Height = 0
		Local Lines = (Text.Split("~n")).Length
		Return (Lines * LineHeight) + (Lines * AdditionalLineSpace)
	End Method
	
	
	
	
	
	Method LoadMetrics(Offset, HMetricsCount)
		Stream.SetPointer(Offset)
		Local Count = 0, LastAdv
		For Local i = 0 To GlyphNumber - 1
			If Count < HMetricsCount - 1 Then
				Glyph[i].Adv = Stream.ReadUInt(2)
				Glyph[i].Lsb = Stream.ReadInt(2)
				LastAdv = Glyph[i].Adv
			Else
				Glyph[i].Adv = LastAdv
				Glyph[i].Lsb = Stream.ReadInt(2)
			End If
			Count = Count + 1
		Next
	End Method
	Method LoadCmapData(Offset)
		Stream.SetPointer(Offset)
		Stream.ReadUInt(2)
		Local numTables = Stream.ReadUInt(2)
		Local PlatformId[numTables]
		Local EncodingId[numTables]
		Local TableOffset[numTables]
		
		'Load all tables and select windows format
		For Local t = 0 To numTables - 1
			PlatformId[t] = Stream.ReadUInt(2)
			EncodingId[t] = Stream.ReadUInt(2)
			TableOffset[t] = Stream.ReadUInt(4) + Offset
		Next
		
		'Load 3-1 first then other tables
		Local WindowsFontFound:Bool = False
		For Local t = 0 To numTables - 1
			If PlatformId[t] = 3 And EncodingId[t] = 1 Then
				WindowsFontFound = True
				Local Format = Stream.PeekUInt(2, TableOffset[t])
				If Format = 0 Then LoadCmapTable0(TableOffset[t] + 2)
				If Format = 4 Then LoadCmapTable4(TableOffset[t] + 2)
				If Format = 6 Then LoadCmapTable6(TableOffset[t] + 2)
			End If
		Next
		
		'Load 3 - Any first Then other tables
		For Local t = 0 To numTables - 1
			If PlatformId[t] = 3 And EncodingId[t] <> 1 Then
				Local Format = Stream.PeekUInt(2, TableOffset[t])
				If Format = 0 Then LoadCmapTable0(TableOffset[t] + 2)
				If Format = 4 Then LoadCmapTable4(TableOffset[t] + 2)
				If Format = 6 Then LoadCmapTable6(TableOffset[t] + 2)
			End If
		Next
		
		'Load Any first Then other tables
		For Local t = 0 To numTables - 1
			If PlatformId[t] <> 3 Then
				Local Format = Stream.PeekUInt(2, TableOffset[t])
				If Format = 0 Then LoadCmapTable0(TableOffset[t] + 2)
				If Format = 4 Then LoadCmapTable4(TableOffset[t] + 2)
				If Format = 6 Then LoadCmapTable6(TableOffset[t] + 2)
			End If
		Next
		
		'Revert additional to 0 - Just to make Sure
		For Local i = 0 To GlyphId.Length - 1
			If GlyphId[i] > GlyphNumber - 1 Then GlyphId[i] = 0
		Next
	End Method
	Method LoadCmapTable0(Offset)
		Stream.SetPointer(Offset)
		Stream.ReadUInt(2); Stream.ReadUInt(2)
		For Local g = 0 To 254
			Local GId = Stream.ReadUInt(1)
			If GlyphId[g] = 0 Then GlyphId[g] = GId
		Next
	End Method
	Method LoadCmapTable4(Offset)
		Local OffCount = Offset
		Stream.SetPointer(Offset)
		Stream.ReadUInt(2); Stream.ReadUInt(2)
		Local SegCount = Stream.ReadUInt(2) / 2
		Local SearchRange = Stream.ReadUInt(2)
		Local EntrySelector = Stream.ReadUInt(2)
		Local RangeShift = Stream.ReadUInt(2)
		OffCount = OffCount + 12
		Local EndCount[SegCount]
		For Local s = 0 To SegCount - 1
			EndCount[s] = Stream.ReadUInt(2)
			OffCount = OffCount + 2
		Next
		Local Reserved = Stream.ReadUInt(2); OffCount = OffCount + 2
		Local StartCount[SegCount]
		For Local s = 0 To SegCount - 1
			StartCount[s] = Stream.ReadUInt(2)
			OffCount = OffCount + 2
		Next
		Local IdDelta[SegCount]
		For Local s = 0 To SegCount - 1
			IdDelta[s] = Stream.ReadInt(2)
			OffCount = OffCount + 2
		Next
		Local IdRangeOffset[SegCount]
		Local IdRangeOffsetOffset[SegCount]
		For Local s = 0 To SegCount - 1
			IdRangeOffset[s] = Stream.ReadUInt(2)
			IdRangeOffsetOffset[s] = OffCount
			OffCount = OffCount + 2
		Next
		'Get Glyph Id
		For Local Char = 0 To GlyphNumber - 1
			Local NullFlag = 1
			'GetSegment
			Local CharSeg
			For Local s = 0 To SegCount - 1
				If EndCount[s] >= Char Then
					CharSeg = s
					If Char >= StartCount[s] Then NullFlag = 0
					Exit
				End If
			Next
			If NullFlag = 1 Then
				GlyphId[Char] = 0
				Continue
			End If
			
			If IdRangeOffset[CharSeg] = 0 Then
				If GlyphId[Char] = 0 Then GlyphId[Char] = IdDelta[CharSeg] + Char
			Else
				Local Location = (2 * (Char - StartCount[CharSeg])) + (IdRangeOffset[CharSeg] - IdRangeOffset[0]) + OffCount + (CharSeg * 2)
				If GlyphId[Char] = 0 Then GlyphId[Char] = Stream.PeekUInt(2, Location)
			End If
		Next
	End Method
	Method LoadCmapTable6(Offset)
		Stream.SetPointer(Offset)
		Stream.ReadUInt(2); Stream.ReadUInt(2)
		Local FirstCode = Stream.ReadUInt(2)
		Local EntryCount = Stream.ReadUInt(2)
		For Local g = FirstCode To EntryCount - 1
			Local GId = Stream.ReadUInt(2)
			If GlyphId[g] = 0 Then GlyphId[g] = GId
		Next
	End Method
	Method LoadLoca(Offset, Format, GlyfOffset)
		Stream.SetPointer(Offset)
		For Local i = 0 To GlyphNumber - 1
			If Format = 0 Then
				Glyph[i].FileAddress = (Stream.ReadUInt(2) * 2) + GlyfOffset
			Else
				Glyph[i].FileAddress = (Stream.ReadUInt(4)) + GlyfOffset
			End If
		Next
	End Method
	Method LoadGlyfData(Id, Offset)
		Stream.SetPointer(Offset)
		'Load contour Number and Position
		Local ContourNumber = Stream.ReadInt(2)
		If ContourNumber < 1 Then Return 0
		Glyph[Id].ContourNumber = ContourNumber
		Glyph[Id].xMin = Stream.ReadInt(2)
		Glyph[Id].yMin = Stream.ReadInt(2)
		Glyph[Id].xMax = Stream.ReadInt(2)
		Glyph[Id].yMax = Stream.ReadInt(2)
		Glyph[Id].W = Glyph[Id].xMax - Glyph[Id].xMin
		Glyph[Id].H = Glyph[Id].yMax - Glyph[Id].yMin
		
		
		
		
		'End Points
		Local EndPoints:Int[ContourNumber]
		For Local i = 0 To ContourNumber - 1
			EndPoints[i] = Stream.ReadUInt(2)
		Next
		Local PointNumber = EndPoints[ContourNumber - 1] + 1
		
		'Instructions
		Local insLen = Stream.ReadUInt(2)
		Stream.ReadString(insLen)
		
		'Flags
		Local Flags:Int[][] = New Int[PointNumber][]
		Local ContinueNumber = 0
		For Local i = 0 To PointNumber - 1
			'Is The Same
			If ContinueNumber > 0 Then
				Flags[i] = Flags[i - 1]
				ContinueNumber = ContinueNumber - 1
				Continue
			End If
			'Load in new flag
			Flags[i] = Stream.ReadBits(1)
			If Flags[i][3] = 1 Then ContinueNumber = Stream.ReadUInt(1)
		Next
		
		
		'XCoords
		Local XCoords:Int[PointNumber]
		For Local i = 0 To PointNumber - 1
			'Is the same as last
			If Flags[i][1] = 0 And Flags[i][4] = 1 Then
				If i > 0 Then XCoords[i] = XCoords[i - 1] Else XCoords[i] = -Glyph[Id].xMin
				Continue
			End If
			'XisByte
			If Flags[i][1] = 1 Then
				Local tmp = Stream.ReadUInt(1)
				If Flags[i][4] = 0 Then tmp = tmp * -1
				If i > 0 Then XCoords[i] = XCoords[i - 1] + tmp Else XCoords[i] = tmp - Glyph[Id].xMin
				Continue
			End If
			If Flags[i][1] = 0 And Flags[i][4] = 0 Then
				If i > 0 Then XCoords[i] = XCoords[i - 1] + Stream.ReadInt(2) Else XCoords[i] = Stream.ReadInt(2) - Glyph[Id].xMin
				Continue
			End If
		Next

		
		'YCoords
'		Glyph[Id].yMax - YVectors[0]
		
		Local YCoords:Int[PointNumber]
		For Local i = 0 To PointNumber - 1
			'Is the same as last
			If Flags[i][2] = 0 And Flags[i][5] = 1 Then
				If i > 0 Then YCoords[i] = YCoords[i - 1] Else YCoords[i] = Glyph[Id].yMax
				Continue
			End If
			'YisByte
			If Flags[i][2] = 1 Then
				Local tmp = Stream.ReadUInt(1)
				If Flags[i][5] = 0 Then tmp = tmp * -1
				If i > 0 Then YCoords[i] = YCoords[i - 1] - tmp Else YCoords[i] = Glyph[Id].yMax - tmp
				Continue
			End If
			If Flags[i][2] = 0 And Flags[i][5] = 0 Then
				If i > 0 Then YCoords[i] = YCoords[i - 1] - Stream.ReadInt(2) Else YCoords[i] = Glyph[Id].yMax - Stream.ReadInt(2)
				Continue
			End If
		Next

		'Transpose to xyList
		Glyph[Id].xyList = New Float[ContourNumber][]
		Local p1 = 0, Pend
		For Local i = 0 To ContourNumber - 1	
			If i > 0 Then
				p1 = EndPoints[i - 1] + 1
			End If
			Pend = EndPoints[i]
			Glyph[Id].xyList[i] = New Float[ ( (Pend - p1 + 1) * 3)]
			Local Count = 0
			For Local j = p1 To Pend
				Glyph[Id].xyList[i][Count] = XCoords[j]
				Glyph[Id].xyList[i][Count + 1] = YCoords[j]
				Glyph[Id].xyList[i][Count + 2] = Flags[j][0]
				Count = Count + 3
			Next
		Next
		
	End Method
	Method QuadGlyph(Id)
		For Local c = 0 To Glyph[Id].ContourNumber - 1
			Local xyStack:Stack<Float> = New Stack<Float>
			For Local p0 = 0 To Glyph[Id].xyList[c].Length - 1 Step 3
				Local p1 = p0 + 3 If p1 > Glyph[Id].xyList[c].Length - 1 Then p1 = 0
				'Add p0
				xyStack.Push(Glyph[Id].xyList[c][p0])
				xyStack.Push(Glyph[Id].xyList[c][p0 + 1])
				xyStack.Push(Glyph[Id].xyList[c][p0 + 2])
				'If Double add a middle point
				If Glyph[Id].xyList[c][p0 + 2] = 0 And Glyph[Id].xyList[c][p1 + 2] = 0 Then
					Local tx:Float = (Glyph[Id].xyList[c][p0] + Glyph[Id].xyList[c][p1]) / 2.0
					Local ty:Float = (Glyph[Id].xyList[c][p0 + 1] + Glyph[Id].xyList[c][p1 + 1]) / 2.0
					xyStack.Push(tx)
					xyStack.Push(ty)
					xyStack.Push(1)
				End If
			Next
			Glyph[Id].xyList[c] = xyStack.ToArray()
		Next
	End Method
	Method SmoothGlyph(Id)
		For Local c = 0 To Glyph[Id].ContourNumber - 1
			Local xyStack:Stack<Float> = New Stack<Float>
			For Local p0 = 0 To Glyph[Id].xyList[c].Length - 1 Step 3
				Local p1 = p0 + 3 If p1 > Glyph[Id].xyList[c].Length - 1 Then p1 = 0
				Local p2 = p1 + 3 If p2 > Glyph[Id].xyList[c].Length - 1 Then p2 = 0
				
				If Glyph[Id].xyList[c][p0 + 2] = 0 Then Continue
				
				'Straight Line
				If Glyph[Id].xyList[c][p0 + 2] = 1 And Glyph[Id].xyList[c][p1 + 2] = 1
					xyStack.Push(Glyph[Id].xyList[c][p0])
					xyStack.Push(Glyph[Id].xyList[c][p0 + 1])
				Else
					'Bexier curve
					Local T:Float[] = CalculateCurve(Glyph[Id].xyList[c][p0], Glyph[Id].xyList[c][p0 + 1], Glyph[Id].xyList[c][p1], Glyph[Id].xyList[c][p1 + 1], Glyph[Id].xyList[c][p2], Glyph[Id].xyList[c][p2 + 1])
					For Local tt:Float = EachIn T
						xyStack.Push(tt)
					Next
				End If
			Next
			Glyph[Id].xyList[c] = xyStack.ToArray()
		Next
		
	End Method
	Method CalculateCurve:Float[] (x1, y1, x2, y2, x3, y3)
		Local Lst:Float[10]
		Local Counter = 0
		For Local t:Float = 0 To 0.8 Step 0.2
			Local tx:Float = (Pow(1.0 - t, 2) * x1) + (2 * ( (1.0 - t) * t * x2)) + (Pow(t, 2) * x3)
			Local ty:Float = (Pow(1.0 - t, 2) * y1) + (2 * ( (1.0 - t) * t * y2)) + (Pow(t, 2) * y3)
			Lst[Counter] = tx
			Lst[Counter + 1] = ty
			Counter = Counter + 2
		Next
		Return Lst
	End Method
	
	Method LoadGlyphImages()
		Local OrigColor:Float[] = GetColor()
		'Copy BG
		Local BW = ( (FontLimits[2] - FontLimits[0]) * FontScale) + 2
		Local BH = ( (FontLimits[3] - FontLimits[1]) * FontScale) + 2
		Local BG:Image = CreateImage(BW, BH)
		Local BGPixels:Int[BW * BH]
		ReadPixels(BGPixels, 0, 0, BW, BH)
		BG.WritePixels(BGPixels, 0, 0, BW, BH)
		
		PushMatrix
		Scale(FontScale, FontScale)
		For Local g = 0 To GlyphNumber - 1
			If Glyph[g].ContourNumber < 1 Then Continue
			Local W = Glyph[g].W * FontScale + 4
			Local H = Glyph[g].H * FontScale + 4
			If W < 1 Or H < 1 Then Continue
			
			'DrawBG
			SetColor(255, 255, 255)
			DrawRect(0, 0, W/FontScale, H/FontScale)
			'Draw solids
			SetColor(0, 0, 0)
			For Local i = 0 To Glyph[g].ContourNumber - 1
				If Glyph[g].Poly[i].Clockwise = FontClockwise Then
					Glyph[g].Poly[i].Draw()
				End If
			Next
			'Draw Cutouts
			SetColor(255, 255, 255)
			For Local i = 0 To Glyph[g].ContourNumber - 1
				If Glyph[g].Poly[i].Clockwise <> FontClockwise Then
					Glyph[g].Poly[i].Draw()
				End If
			Next
			
			'SaveImage
			Local pixels:Int[W * H]
			ReadPixels(pixels, 0, 0, W, H)
			
			'Set Alpha & Color
			For Local i:Int = 0 Until pixels.Length
	        	Local argb:Int = pixels[i]
	        	Local a:Int = (argb Shr 24) & $ff
	        	Local r:Int = (argb Shr 16) & $ff
	        	Local g:Int = (argb Shr 8) & $ff
	        	Local b:Int = argb & $ff
	        	a = 255 - r
	        	r = Color[0]
				g = Color[1]
				b = Color[2]
	        	argb = (a Shl 24) | (r Shl 16) | (g Shl 8) | b
	        	pixels[i] = argb
			Next
			Glyph[g].Img = CreateImage(W, H)
			Glyph[g].Img.WritePixels(pixels, 0, 0, W, H)
		Next
		PopMatrix
		SetColor(255, 255, 255)
		DrawImage(BG, 0, 0)
		ImagesLoaded = True
		SetColor(OrigColor[0], OrigColor[1], OrigColor[2])
	End Method
	
	
	
End Class


Class TFont_Glyph
	Field Adv, Lsb
	Field FileAddress
	Field xMin, yMin, xMax, yMax, W, H
	Field ContourNumber
	Field Points:Int[][]
	Field xyList:Float[][]
	Field Poly:TFont_Poly[]
	Field Img:Image
	
End Class





	


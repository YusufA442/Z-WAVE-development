Import mojo
Import brl.databuffer

Class DataStream
	'Byte = 8 Bits
	'U = Unsigned (i.e. ReadUInt = ReadUnsignedInt)
	'ReadInt(ByteCount) - I type Int and mean number - by using adjusting 'ByteCount' it can technically be a byte(1), short(2) or Long(4) etc
	Field Buffer:DataBuffer
	Field Pointer
	Field BigEndian:Bool
	
	'Create New Datastream
	Method New(Path:String, BigEndianFormat:Bool = True)
		Buffer = DataBuffer.Load(Path)
		Pointer = 0
		BigEndian = BigEndianFormat
	End Method
	
	'Sets the Datastream pointer location (offset from strat of file in bytes)
	Method SetPointer(Offset)
		Pointer = Offset
	End Method
	
	'Sets the Datastream pointer location (offset from strat of file in bytes)
	Method GetPointer()
		Return Pointer
	End Method
	
	'Read Methods
	Method ReadInt(ByteCount)
		Pointer = Pointer + ByteCount
		If Not BigEndian Then Return CalculateBits(ChangeEndian(BytesToArr(Pointer - ByteCount, ByteCount)))
		Return CalculateBits(BytesToArr(Pointer - ByteCount, ByteCount))
	End Method
	Method ReadUInt(ByteCount)
		Pointer = Pointer + ByteCount
		If Not BigEndian Then Return CalculateUBits(ChangeEndian(BytesToArr(Pointer - ByteCount, ByteCount)))
		Return CalculateUBits(BytesToArr(Pointer - ByteCount, ByteCount))
	End Method
	Method ReadFixed32:Float()
		Return Float(ReadInt(2) + "." + ReadInt(2))
	End Method
	Method ReadString:String(ByteCount)
		Pointer = Pointer + ByteCount
		Return Buffer.PeekString(Pointer - ByteCount, ByteCount)
	End Method
	Method ReadBits:Int[] (ByteCount)
		Local Str:Int[] = BytesToArr(Pointer, ByteCount)
		Pointer = Pointer + ByteCount
		If Not BigEndian Then Str = ChangeEndian(Str)
		Local temp
		For Local i = 0 Until Str.Length / 2
			temp = Str[i]
			Str[i] = Str[Str.Length - i - 1]
			Str[Str.Length - i - 1] = temp
		Next
		Return Str
	End Method
	
	'Peek Methods
	Method PeekInt(ByteCount, Address)
		If Not BigEndian Then Return CalculateBits(ChangeEndian(BytesToArr(Address, ByteCount)))
		Return CalculateBits(BytesToArr(Address, ByteCount))
	End Method
	Method PeekUInt(ByteCount, Address)
		If Not BigEndian Then CalculateUBits(ChangeEndian(BytesToArr(Address, ByteCount)))
		Return CalculateUBits(BytesToArr(Address, ByteCount))
	End Method
	Method PeekFixed32:Float(Address)
		Return Float(PeekInt(2, Address) + "." + PeekInt(2, Address + 2))
	End Method
	Method PeekString:String(ByteCount, Address)
		Return Buffer.PeekString(Address, ByteCount)
	End Method
	Method PeekBits:Int[] (ByteCount, Address)
		Local Str:Int[] = BytesToArr(Address, ByteCount)
		If Not BigEndian Then Str = ChangeEndian(Str)
		'reverse str
		Local temp
		For Local i = 0 Until Str.Length / 2
			temp = Str[i]
			Str[i] = Str[Str.Length - i - 1]
			Str[Str.Length - i - 1] = temp
		Next
		Return Str
	End Method
	
	'Converts Bit array to String - Helpfull for debug	
	Function ToString:String(Bits:Int[])
		Local Rtn:String
		For Local i = 0 To Bits.Length - 1
			Rtn = Rtn + Bits[i]
		Next
		Return Rtn
	End Function
	
	Function ChangeEndian:Int[] (BitString:Int[])
		If BitString.Length < 16 Then Return BitString
		Local t
		For Local b = 0 To(BitString.Length - 1) / 2 Step 8
			For Local i = 0 To 7
				t = BitString[b + i]
				BitString[b + i] = BitString[BitString.Length - 8 - b + i]
				BitString[BitString.Length - 8 - b + i] = t
			Next
		Next
		Return BitString
	End Function
	Method BytesToArr:Int[] (Address, Count)
		Local Str:Int[Count * 8], Counter = 0
		For Local i = 0 To Count - 1
			Local Byt:Int[] = ByteToArr(Address + i)
			For Local c = 0 To 7
				Str[Counter] = Byt[c]
				Counter = Counter + 1
			Next
		Next
		Return Str
	End Method	
	Method ByteToArr:Int[] (Address)
		Local I:Int = Buffer.PeekByte(Address)
		Local Str:Int[8]
		'If I = Positive
		If I > - 1 Then
			'Create Bits
			Local D = 128, Counter = 0
			While I > 0
				If I >= D Then
					Str[Counter] = 1
					I = I - D
				Else
					Str[Counter] = 0
				End If
				D = D / 2
				Counter = Counter + 1
			Wend
			'Pad Out
			While Counter < 8
				Str[Counter] = 0
				Counter = Counter + 1
			Wend
			Return Str
		End If
		
		'If I = Negative

		I = I * -1
		'Create Bits (and Flip)
		Local D = 128, Counter = 0
		While I > 0
			If I >= D Then
				Str[Counter] = 1
				I = I - D
			Else
				Str[Counter] = 0
			End If
			D = D / 2
			Counter = Counter + 1
		Wend
		'Pad Out
		While Str.Length < 8
			Str[Counter] = 0
			Counter = Counter + 1
		Wend
		'Flip
		For Local i = 7 To 0 Step - 1
			If Str[i] = 0 Then
				Str[i] = 1
			Else
				Str[i] = 0
			End If
		Next
		'Add 1
		For Local i = 7 To 0 Step - 1
			If Str[i] = 0 Then
				Str[i] = 1
				Exit
			Else
				Str[i] = 0
			End If
		Next
		Return Str
	End Method

	Function CalculateUBits(BitString:Int[])
		Local Rtn:Int, D:Int = 1
		For Local i =  BitString.Length - 1 To 0 Step -1
			If BitString[i] = 1 Then
				Rtn = Rtn + D
			End If
			D = D * 2
		Next
		Return Rtn
	End Function
	Function CalculateBits(BitString:Int[])
		'If Positive
		If BitString[0] = 0 Then
			Local Rtn:Int, D:Int = 1
			For Local i = BitString.Length - 1 To 0 Step - 1
				If BitString[i] = 1 Then
					Rtn = Rtn + D
				End If
				D = D * 2
			Next
			Return Rtn
		End If
		
		'===If Negative
		'Flip Bits and into array
		For Local i = 0 To BitString.Length - 1
			If BitString[i] = 0 Then
				BitString[i] = 1
			Else
				BitString[i] = 0
			End If
		Next
		'Add 1
		For Local i = BitString.Length - 1 To 0 Step - 1
			If BitString[i] = 0 Then
				BitString[i] = 1
				Exit
			Else
				BitString[i] = 0
			End If
		Next
		'Add Up
		Local Rtn:Int, D:Int = 1
		For Local i = BitString.Length - 1 To 0 Step - 1
			If BitString[i] = 1 Then
				Rtn = Rtn + D
			End If
			D = D * 2
		Next
		Return Rtn*-1
		
	End Function
	
End Class
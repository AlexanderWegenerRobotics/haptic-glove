#include "Networking/SimProtocol.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const uint8 StateMagic[4] = {'H', 'G', 'S', 'T'};
	const uint8 SceneMagic[4] = {'H', 'G', 'S', 'C'};

	struct FReader
	{
		const uint8* Data;
		int32 Size;
		int32 Offset = 0;
		bool bOk = true;

		/** Read one little endian value, flagging an overrun instead of reading past the end. */
		template <typename T>
		T Read()
		{
			T Value{};
			if (Offset + static_cast<int32>(sizeof(T)) > Size)
			{
				bOk = false;
				return Value;
			}
			FMemory::Memcpy(&Value, Data + Offset, sizeof(T));
			Offset += sizeof(T);
			return Value;
		}

		/** Read seven floats (position, w x y z) as an Unreal transform. */
		FTransform ReadPose()
		{
			float V[7];
			for (float& F : V)
			{
				F = Read<float>();
			}
			return FTransform(SimProtocol::ToUnreal(V[3], V[4], V[5], V[6]), SimProtocol::ToUnreal(V[0], V[1], V[2]));
		}
	};

	/** JSON number array to a vector, padding missing entries with zero. */
	FVector ReadVector(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		FVector V = FVector::ZeroVector;
		for (int32 i = 0; i < FMath::Min(3, Array.Num()); ++i)
		{
			V[i] = Array[i]->AsNumber();
		}
		return V;
	}

	/** JSON position array in meters to an Unreal location. */
	FVector ReadPosition(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		const FVector V = ReadVector(Array);
		return SimProtocol::ToUnreal(V.X, V.Y, V.Z);
	}

	/** JSON quaternion array (w, x, y, z) to an Unreal rotation. */
	FQuat ReadRotation(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		if (Array.Num() < 4)
		{
			return FQuat::Identity;
		}
		return SimProtocol::ToUnreal(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber(), Array[3]->AsNumber());
	}

	/** JSON rgba array in sRGB to a linear color. */
	FLinearColor ReadColor(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		float C[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		for (int32 i = 0; i < FMath::Min(4, Array.Num()); ++i)
		{
			C[i] = static_cast<float>(Array[i]->AsNumber());
		}
		FLinearColor Color = FLinearColor::FromSRGBColor(FLinearColor(C[0], C[1], C[2]).ToFColor(false));
		Color.A = C[3];
		return Color;
	}
}

FVector SimProtocol::ToUnreal(double X, double Y, double Z)
{
	return FVector(100.0 * X, -100.0 * Y, 100.0 * Z);
}

FQuat SimProtocol::ToUnreal(double W, double X, double Y, double Z)
{
	FQuat Q(-X, Y, -Z, W);
	Q.Normalize();
	return Q;
}

double SimProtocol::UnixNow()
{
	return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
}

bool SimProtocol::ParseState(const uint8* Data, int32 Size, FSimState& Out)
{
	if (Size < 4 || FMemory::Memcmp(Data, StateMagic, 4) != 0)
	{
		return false;
	}
	FReader R{Data, Size, 4};
	if (R.Read<uint16>() != Version)
	{
		return false;
	}
	Out.HandType = R.Read<uint8>();
	Out.Flags = R.Read<uint8>();
	Out.Seq = R.Read<uint32>();
	Out.SimTime = R.Read<double>();
	Out.SendTime = R.Read<double>();
	ANSICHAR Id[9] = {};
	for (int32 i = 0; i < 8; ++i)
	{
		Id[i] = static_cast<ANSICHAR>(R.Read<uint8>());
	}
	Out.SceneId = FString(ANSI_TO_TCHAR(Id));
	Out.Trial = R.Read<uint32>();
	Out.CommandSeq = R.Read<uint32>();
	Out.Countdown = R.Read<float>();
	Out.Wrist = R.ReadPose();
	Out.Ghost = R.ReadPose();

	const uint16 NumJoints = R.Read<uint16>();
	Out.Joints.SetNum(NumJoints);
	for (float& J : Out.Joints)
	{
		J = R.Read<float>();
	}
	const uint16 NumBodies = R.Read<uint16>();
	Out.Bodies.SetNum(NumBodies);
	for (FTransform& B : Out.Bodies)
	{
		B = R.ReadPose();
	}
	for (float& C : Out.Closure)
	{
		C = R.Read<float>();
	}
	for (float& F : Out.Feedback)
	{
		F = R.Read<float>();
	}
	const uint16 NumObjects = R.Read<uint16>();
	Out.Objects.SetNum(NumObjects);
	for (FSimObjectState& O : Out.Objects)
	{
		O.Pose = R.ReadPose();
		O.SquashDepth = 100.0f * R.Read<float>();
		const float DX = R.Read<float>();
		const float DY = R.Read<float>();
		const float DZ = R.Read<float>();
		O.SquashDir = FVector(DX, -DY, DZ);
	}
	return R.bOk;
}

bool SimProtocol::ParseScene(const uint8* Data, int32 Size, FSimScene& Out)
{
	if (Size < 4 || FMemory::Memcmp(Data, SceneMagic, 4) != 0)
	{
		return false;
	}
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + 4), Size - 4);
	const FString Json(Converted.Length(), Converted.Get());
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return false;
	}

	Out = FSimScene();
	Out.SceneId = Root->GetStringField(TEXT("scene_id"));
	Out.SceneName = Root->GetStringField(TEXT("scene_name"));
	Out.Hand = Root->GetStringField(TEXT("hand"));
	Root->TryGetStringField(TEXT("environment"), Out.Environment);

	for (const TSharedPtr<FJsonValue>& Name : Root->GetArrayField(TEXT("hand_bodies")))
	{
		Out.HandBodies.Add(Name->AsString());
	}
	for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("hand_geoms")))
	{
		const TSharedPtr<FJsonObject> G = Value->AsObject();
		FSimGeom Geom;
		Geom.Body = static_cast<int32>(G->GetNumberField(TEXT("body")));
		Geom.Type = G->GetStringField(TEXT("type"));
		Geom.Size = 100.0 * ReadVector(G->GetArrayField(TEXT("size")));
		Geom.Local = FTransform(ReadRotation(G->GetArrayField(TEXT("orientation"))),
		                        ReadPosition(G->GetArrayField(TEXT("position"))));
		Geom.Color = ReadColor(G->GetArrayField(TEXT("rgba")));
		Out.HandGeoms.Add(Geom);
	}

	const TSharedPtr<FJsonObject> Table = Root->GetObjectField(TEXT("table"));
	Out.Table.TopCenter = ReadPosition(Table->GetArrayField(TEXT("position")));
	Out.Table.Size = 100.0 * ReadVector(Table->GetArrayField(TEXT("size")));
	Out.Table.Height = static_cast<float>(100.0 * Table->GetNumberField(TEXT("height")));
	Out.Table.Color = ReadColor(Table->GetArrayField(TEXT("rgba")));

	for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("objects")))
	{
		const TSharedPtr<FJsonObject> O = Value->AsObject();
		FSimObject Object;
		Object.Id = O->GetStringField(TEXT("id"));
		Object.Type = O->GetStringField(TEXT("type"));
		Object.Shape = O->GetStringField(TEXT("shape"));
		Object.Size = 100.0 * ReadVector(O->GetArrayField(TEXT("size")));
		Object.Color = ReadColor(O->GetArrayField(TEXT("rgba")));
		Object.bDeformable = O->GetBoolField(TEXT("deformable"));
		O->TryGetStringField(TEXT("visual"), Object.Visual);
		Object.Initial = FTransform(ReadRotation(O->GetArrayField(TEXT("orientation"))),
		                            ReadPosition(O->GetArrayField(TEXT("position"))));
		Out.Objects.Add(Object);
	}

	const TSharedPtr<FJsonObject>* Tracking = nullptr;
	if (Root->TryGetObjectField(TEXT("tracking"), Tracking) && Tracking && Tracking->IsValid())
	{
		const TSharedPtr<FJsonObject>& T = *Tracking;
		Out.Tracking.bValid = T->HasTypedField<EJson::String>(TEXT("created"));
		Out.Tracking.YawRad = static_cast<float>(T->GetNumberField(TEXT("yaw")));
		Out.Tracking.Position = ReadPosition(T->GetArrayField(TEXT("position")));
		T->TryGetStringField(TEXT("created"), Out.Tracking.Created);
	}

	const TSharedPtr<FJsonObject>* Session = nullptr;
	if (Root->TryGetObjectField(TEXT("session"), Session) && Session && Session->IsValid())
	{
		(*Session)->TryGetStringField(TEXT("id"), Out.SessionId);
		(*Session)->TryGetStringField(TEXT("directory"), Out.SessionDirectory);
	}
	return true;
}

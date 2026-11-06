using Godot;
using Godot.Collections;

/// <summary>A weapon described in C#.</summary>
[GlobalClass]
public partial class WeaponData : Resource
{
    public enum DamageType { Physical, Fire, Ice }

    [Export] public string DisplayName { get; set; } = "Sword";
    [Export(PropertyHint.Range, "1,200,1")] public int Damage { get; set; } = 10;
    [Export(PropertyHint.Range, "0.1,10,0.1")] public float Cooldown = 0.5f;
    [Export] public DamageType Type { get; set; } = DamageType.Physical;
    [ExportGroup("Visuals")]
    [Export(PropertyHint.File, "*.png")] public string Icon { get; set; } = "";
    [Export] public Color Trail { get; set; } = new Color(1, 0.5f, 0);
    [Export] public Array<string> Tags { get; set; } = new();

    private int _internalCounter;

    public override string ToString() => DisplayName;
}

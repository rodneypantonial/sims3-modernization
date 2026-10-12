using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.Metadata;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;
using Sims3.SimIFace.CAS;
using Sims3.SimIFace.CustomContent;

namespace Ts3Agent
{
    // Store content in use (docs/17 §4 M9): list buy objects and CAS parts by origin, place objects on the home lot and
    // dress Sims in parts, then undo both, so commitMB can be diffed per item in use. Everything is undoable and nothing
    // saves: objects are created free (no funds change) and only objects created here can be removed; parts go into a
    // separate Special outfit (the FirewalkPit barefoot pattern), the Sim's own outfits are never edited.
    // Origin = the game's own content tracker, DownloadContent.GetContentCategory (what the buy/CAS custom-content icon
    // shows): kInstalled = "ea", kFree/PaidDownloadedEA + kFeaturedStoreItem = "store", kUser/LocalUserCreated = "cc".
    // pack = ProductID in the key's group id (bits 27-31, ContentCategoryHandler), as ProductVersion (BaseGame, EP1, ...).
    public static partial class Handlers
    {
        static void RegisterContent()
        {
            Dispatcher.Register("catalog_objects", CatalogObjects);
            Dispatcher.Register("place_objects", PlaceObjects);
            Dispatcher.Register("remove_objects", RemoveObjects);
            Dispatcher.Register("cas_parts", CasParts);
            Dispatcher.Register("wear_parts", WearParts);
            Dispatcher.Register("restore_outfit", RestoreOutfit);
        }

        const string kWearOutfit = "Ts3AgentWear";

        // Undo records live in AppDomain data so a hot plugin reload does not lose them.
        static T DomainState<T>(string name) where T : class, new()
        {
            T v = AppDomain.CurrentDomain.GetData(name) as T;
            if (v == null) { v = new T(); AppDomain.CurrentDomain.SetData(name, v); }
            return v;
        }
        static List<ulong> CreatedObjects() { return DomainState<List<ulong>>("ts3agent.content.created"); }
        // SimDescriptionId -> {outfit category, index} worn before wear_parts
        static Dictionary<ulong, int[]> WornBefore() { return DomainState<Dictionary<ulong, int[]>>("ts3agent.content.worn"); }

        static string ContentOrigin(ResourceKey k)
        {
            switch (DownloadContent.GetContentCategory(k) & ResourceKeyContentCategory.kResourceKeyContentCategoryMask)
            {
                case ResourceKeyContentCategory.kInstalled: return "ea";
                case ResourceKeyContentCategory.kUserCreated:
                case ResourceKeyContentCategory.kLocalUserCreated: return "cc";
                default: return "store";
            }
        }

        static void AddOrigin(Dictionary<string, object> d, ResourceKey k)
        {
            d["key"] = KeyString(k);
            d["origin"] = ContentOrigin(k);
            d["category"] = (DownloadContent.GetContentCategory(k) & ResourceKeyContentCategory.kResourceKeyContentCategoryMask).ToString();
            d["source"] = DownloadContent.GetContentSource(k).ToString();
            d["pack"] = ((ProductVersion)(1u << (int)ContentCategoryHandler.ProductID(k.GroupId))).ToString();
        }

        static string OriginFilter(Args a)
        {
            string f = a.Str("filter", "all").Trim().ToLowerInvariant();
            if (f != "all" && f != "store" && f != "cc" && f != "ea") throw new McpException("filter must be store, cc, ea or all");
            return f;
        }

        static void RequireLive()
        {
            if (SubState() != "LiveModeState") throw new McpException("not in live mode (state " + SubState() + ")");
        }

        // Buy catalog sorted by key, so filter+max picks the same items every run.
        static List<BuildBuyProduct> SortedCatalog()
        {
            List<BuildBuyProduct> list = new List<BuildBuyProduct>(Catalog());
            list.Sort(delegate(BuildBuyProduct x, BuildBuyProduct y) { return string.CompareOrdinal(KeyString(x.ProductResourceKey), KeyString(y.ProductResourceKey)); });
            return list;
        }

        // catalog_objects {filter?, max?}
        static object CatalogObjects(Args a)
        {
            string f = OriginFilter(a);
            int max = (int)a.Long("max", 50);
            List<object> items = new List<object>();
            int matched = 0;
            foreach (BuildBuyProduct p in SortedCatalog())
            {
                ResourceKey k = p.ProductResourceKey;
                if (f != "all" && ContentOrigin(k) != f) continue;
                matched++;
                if (items.Count >= max) continue;
                Dictionary<string, object> d = Util.Obj();
                AddOrigin(d, k);
                d["name"] = p.CatalogName;
                d["instance"] = p.ObjectInstanceName;
                d["price"] = (int)p.Price;
                d["placeable"] = Placeable(p);
                items.Add(d);
            }
            Dictionary<string, object> r = Util.Obj();
            r["matched"] = matched;
            r["catalog"] = Catalog().Count;
            r["items"] = items;
            return r;
        }

        // What place_objects picks by filter: floor objects that are not animals, horses, boats or underwater objects.
        static bool Placeable(BuildBuyProduct p)
        {
            if (p.IsWallObject) return false;
            if ((p.BuyCategoryFlags & (uint)BuildBuyProduct.eBuyCategory.kBuyCategoryPets) != 0) return false;
            if ((p.BuySubCategoryFlags & (ulong)(BuildBuyProduct.eBuySubCategory.kBuySubCategoryHorses
                | BuildBuyProduct.eBuySubCategory.kBuySubCategoryDogs | BuildBuyProduct.eBuySubCategory.kBuySubCategoryCats)) != 0) return false;
            return (p.BuySubCategory2Flags & (ulong)(BuildBuyProduct.eBuySubCategory2.kBuySubCategory2Boats
                | BuildBuyProduct.eBuySubCategory2.kBuySubCategory2UnderwaterObjects)) == 0;
        }

        // place_objects {keys:[...] | filter, count?, lot?: "home"}: create objects free of charge at free spots on the active
        // household's home lot (the game's FindGoodLocationNearby from the lot centre: never on top of, nor moving, existing
        // objects); a spot off the lot or none at all -> the family inventory. Returns the ids for remove_objects.
        static object PlaceObjects(Args a)
        {
            RequireLive();
            if (a.Has("lot") && a.Str("lot") != "home") throw new McpException("only lot: \"home\" is supported");
            Household hh = Household.ActiveHousehold;
            Lot lot = hh != null ? hh.LotHome : null;
            if (lot == null) throw new McpException("the active household has no home lot");
            List<ResourceKey> keys = new List<ResourceKey>();
            if (a.Has("keys"))
            {
                List<object> ks = a.List("keys");
                if (ks.Count == 0) throw new McpException("keys is empty");
                int n = (int)a.Long("count", ks.Count);
                for (int i = 0; i < n; i++) keys.Add(ParseKey(Convert.ToString(ks[i % ks.Count])));
            }
            else
            {
                string f = OriginFilter(a);
                int n = (int)a.Long("count");
                foreach (BuildBuyProduct p in SortedCatalog())
                {
                    if (keys.Count >= n) break;
                    if (Placeable(p) && (f == "all" || ContentOrigin(p.ProductResourceKey) == f)) keys.Add(p.ProductResourceKey);
                }
                if (keys.Count < n) throw new McpException("only " + keys.Count + " placeable '" + f + "' objects in the catalog");
            }
            if (keys.Count > 100) throw new McpException("count is capped at 100");
            List<ulong> created = CreatedObjects();
            List<object> placed = new List<object>();
            Vector3 centre = lot.GetCenterPosition();
            foreach (ResourceKey k in keys)
            {
                Dictionary<string, object> d = Util.Obj();
                AddOrigin(d, k);
                placed.Add(d);
                GameObject go = GlobalFunctions.CreateObjectOutOfWorld(k) as GameObject;
                if (go == null) { d["error"] = "the game could not create it"; continue; }
                created.Add(go.ObjectId.Value);
                d["id"] = Util.Id(go.ObjectId);
                d["name"] = Util.ObjectName(go);
                Vector3 pos = centre, fwd = Vector3.UnitZ;
                if (GlobalFunctions.FindGoodLocationNearby(go, ref pos, ref fwd) && LotManager.GetLotAtPoint(pos) == lot)
                {
                    go.SetPosition(pos);
                    go.SetForward(fwd);
                    go.AddToWorld();
                    d["where"] = "lot";
                    d["position"] = Pos(pos);
                }
                else if (hh.SharedFamilyInventory != null && hh.SharedFamilyInventory.Inventory.TryToAdd(go))
                    d["where"] = "family inventory";
                else
                {
                    go.Destroy();
                    created.Remove(go.ObjectId.Value);
                    d.Remove("id");
                    d["error"] = "no free spot on the lot and the family inventory refused it";
                }
            }
            Dictionary<string, object> r = Util.Obj();
            r["lot"] = lot.Name;
            r["objects"] = placed;
            r["tracked"] = created.Count;
            return r;
        }

        // remove_objects {ids?:[...]}: destroy objects that place_objects created (all of them when ids is left out);
        // any other id is refused. An object a Sim is using is left alone and reported; retry it later.
        static object RemoveObjects(Args a)
        {
            List<ulong> created = CreatedObjects();
            List<ulong> ids = new List<ulong>();
            if (a.Has("ids")) foreach (object o in a.List("ids")) ids.Add(Convert.ToUInt64(o, System.Globalization.CultureInfo.InvariantCulture));
            else ids.AddRange(created);
            List<object> removed = new List<object>(), kept = new List<object>();
            foreach (ulong id in ids)
            {
                if (!created.Contains(id)) { kept.Add(Util.Id(id) + ": not created by place_objects"); continue; }
                GameObject go = GameObject.GetObject(new ObjectGuid(id));
                if (go == null) { created.Remove(id); removed.Add(Util.Id(id) + " (already gone)"); continue; }
                if (go.InUse) { kept.Add(Util.Id(id) + ": in use"); continue; }
                Inventory inv = Inventory.ParentInventory(go);
                if (inv != null) inv.TryToRemove(go);
                go.Destroy();
                created.Remove(id);
                removed.Add(Util.Id(id));
            }
            Dictionary<string, object> r = Util.Obj();
            r["removed"] = removed;
            r["kept"] = kept;
            r["tracked"] = created.Count;
            return r;
        }

        // A part fits when it covers the Sim's age and gender and has the same species (none = human), as OutfitUtils does.
        static bool Fits(CASPart p, CASAgeGenderFlags ags)
        {
            CASAgeGenderFlags pa = p.AgeGenderSpecies;
            if ((pa & CASAgeGenderFlags.SpeciesMask) == 0) pa |= CASAgeGenderFlags.Human;
            if ((ags & CASAgeGenderFlags.SpeciesMask) == 0) ags |= CASAgeGenderFlags.Human;
            CASAgeGenderFlags ag = CASAgeGenderFlags.AgeMask | CASAgeGenderFlags.GenderMask;
            return (pa & ags & ag) == (ags & ag) && (pa & CASAgeGenderFlags.SpeciesMask) == (ags & CASAgeGenderFlags.SpeciesMask);
        }

        // cas_parts {filter?, max?, sim?, body_type?}: every CAS part (PartSearch, the list OutfitUtils draws from), sorted
        // by key; sim = only parts that fit that Sim; body_type = a BodyTypes name (Hair, UpperBody, Shoes, ...).
        static object CasParts(Args a)
        {
            string f = OriginFilter(a);
            int max = (int)a.Long("max", 50);
            bool bySim = a.Has("sim");
            CASAgeGenderFlags ags = bySim ? Util.FindSimDescription(a, "sim").AgeGenderSpecies : CASAgeGenderFlags.None;
            BodyTypes bt = BodyTypes.None;
            if (a.Has("body_type"))
            {
                try { bt = (BodyTypes)Enum.Parse(typeof(BodyTypes), a.Str("body_type"), true); }
                catch (ArgumentException) { throw new McpException("unknown body_type '" + a.Str("body_type") + "'"); }
            }
            List<CASPart> parts = new List<CASPart>();
            Dictionary<ResourceKey, bool> seen = new Dictionary<ResourceKey, bool>();
            foreach (CASPart p in new PartSearch())
            {
                if (seen.ContainsKey(p.Key)) continue;
                seen[p.Key] = true;
                if (bt != BodyTypes.None && p.BodyType != bt) continue;
                if (bySim && !Fits(p, ags)) continue;
                if (f != "all" && ContentOrigin(p.Key) != f) continue;
                parts.Add(p);
            }
            parts.Sort(delegate(CASPart x, CASPart y) { return string.CompareOrdinal(KeyString(x.Key), KeyString(y.Key)); });
            List<object> items = new List<object>();
            for (int i = 0; i < parts.Count && i < max; i++)
            {
                Dictionary<string, object> d = Util.Obj();
                AddOrigin(d, parts[i].Key);
                d["body_type"] = parts[i].BodyType.ToString();
                d["age_gender"] = parts[i].AgeGenderSpecies.ToString();
                d["name"] = CASUtils.PartDataGetName(parts[i].Key);
                items.Add(d);
            }
            Dictionary<string, object> r = Util.Obj();
            r["matched"] = parts.Count;
            r["items"] = items;
            return r;
        }

        // wear_parts {sim?, keys:[...]}: current outfit + the parts (each replaces what the Sim wears on that body type;
        // full body and top/bottom replace each other; a later key of the same body type wins) as a new Special outfit,
        // switched to at once. Parts that do not fit the Sim are skipped and reported. restore_outfit undoes it.
        static object WearParts(Args a)
        {
            RequireLive();
            Sim s = Util.FindSim(a, "sim");
            SimDescription sd = s.SimDescription;
            if (WornBefore().ContainsKey(sd.SimDescriptionId)) throw new McpException(sd.FullName + " already wears agent parts; restore_outfit first");
            if (sd.IsSupernaturalForm) throw new McpException(sd.FullName + " is in a supernatural form; its outfit cannot be swapped");
            List<object> worn = new List<object>(), skipped = new List<object>();
            SimBuilder b = new SimBuilder();
            try
            {
                b.UseCompression = true;
                OutfitUtils.SetOutfit(b, s.CurrentOutfit, sd, false);
                foreach (object o in a.List("keys"))
                {
                    string ks = Convert.ToString(o);
                    CASPart p = new CASPart(ParseKey(ks));
                    if (p.Key == ResourceKey.kInvalidResourceKey || p.BodyType == BodyTypes.None) { skipped.Add(ks + ": no such CAS part"); continue; }
                    if (!Fits(p, sd.AgeGenderSpecies)) { skipped.Add(ks + ": " + p.AgeGenderSpecies + " does not fit " + sd.AgeGenderSpecies); continue; }
                    if (p.BodyType == BodyTypes.FullBody) b.RemoveParts(BodyTypes.FullBody, BodyTypes.UpperBody, BodyTypes.LowerBody);
                    else if (p.BodyType == BodyTypes.UpperBody || p.BodyType == BodyTypes.LowerBody) b.RemoveParts(p.BodyType, BodyTypes.FullBody);
                    else b.RemoveParts(p.BodyType);
                    if (!b.AddPart(p)) { skipped.Add(ks + ": SimBuilder refused it"); continue; }
                    Dictionary<string, object> d = Util.Obj();
                    AddOrigin(d, p.Key);
                    d["body_type"] = p.BodyType.ToString();
                    worn.Add(d);
                }
                if (worn.Count == 0) throw new McpException("no part could be worn: " + string.Join("; ", skipped.ConvertAll(Convert.ToString).ToArray()));
                ResourceKey key = b.CacheOutfit(kWearOutfit + sd.SimDescriptionId);
                int[] before = { (int)s.CurrentOutfitCategory, s.CurrentOutfitIndex };
                int idx = sd.AddSpecialOutfit(new SimOutfit(key), kWearOutfit);
                if (idx < 0) throw new McpException("the game refused the outfit");
                WornBefore()[sd.SimDescriptionId] = before;
                s.SwitchToOutfitWithoutSpin(OutfitCategories.Special, sd.GetOutfit(OutfitCategories.Special, idx), idx);
            }
            finally { b.Dispose(); }
            Dictionary<string, object> r = Util.Obj();
            r["sim"] = sd.FullName;
            r["worn"] = worn;
            r["skipped"] = skipped;
            return r;
        }

        // restore_outfit {sim?}: back to the outfit worn before wear_parts (if the Sim still wears ours) and the agent
        // outfit removed from the Sim. sim: "all" restores every Sim wear_parts dressed.
        static object RestoreOutfit(Args a)
        {
            Dictionary<ulong, int[]> rec = WornBefore();
            List<ulong> ids = new List<ulong>();
            if (a.Has("sim") && a.Str("sim") == "all") ids.AddRange(rec.Keys);
            else ids.Add(Util.FindSimDescription(a, "sim").SimDescriptionId);
            List<object> done = new List<object>();
            foreach (ulong id in ids)
            {
                SimDescription sd = SimDescription.Find(id);
                if (sd == null) { rec.Remove(id); continue; }
                int[] before;
                if (!rec.TryGetValue(id, out before)) throw new McpException(sd.FullName + " wears no agent parts");
                Sim s = sd.CreatedSim;
                int ours = sd.HasSpecialOutfit(kWearOutfit) ? sd.GetSpecialOutfitIndexFromKey(ResourceUtils.HashString32(kWearOutfit)) : -1;
                bool switched = false;
                if (s != null && ours >= 0 && s.CurrentOutfitCategory == OutfitCategories.Special && s.CurrentOutfitIndex == ours)
                {
                    switched = s.SwitchToOutfitWithoutSpin((OutfitCategories)before[0], before[1]);
                    if (!switched) s.SwitchToOutfitWithoutSpin(Sim.ClothesChangeReason.Force, OutfitCategories.Everyday);
                }
                sd.RemoveSpecialOutfit(kWearOutfit);
                rec.Remove(id);
                done.Add(sd.FullName + (switched ? "" : " (outfit removed; previous outfit not re-applied)"));
            }
            Dictionary<string, object> r = Util.Obj();
            r["restored"] = done;
            return r;
        }
    }
}

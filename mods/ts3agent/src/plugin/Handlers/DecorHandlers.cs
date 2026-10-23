using System;
using System.Collections.Generic;
using Sims3.Gameplay;
using Sims3.Gameplay.Abstracts;
using Sims3.Gameplay.Actors;
using Sims3.Gameplay.CAS;
using Sims3.Gameplay.Core;
using Sims3.SimIFace;
using Sims3.SimIFace.BuildBuy;

namespace Ts3Agent
{
    // Exact furnishing: place at a precise position/facing, swap an object in place, dump the layout.
    // Facing is in degrees: 0 = +z, 90 = +x, 180 = -z, 270 = -x.
    public static partial class Handlers
    {
        static float Facing(Vector3 fwd)
        {
            double deg = Math.Atan2(fwd.x, fwd.z) * 180.0 / Math.PI;
            if (deg < 0) deg += 360.0;
            return (float)Math.Round(deg, 1);
        }

        static Vector3 FacingVector(double deg)
        {
            double r = deg * Math.PI / 180.0;
            return new Vector3((float)Math.Sin(r), 0f, (float)Math.Cos(r));
        }

        static BuildBuyProduct FindProduct(string key)
        {
            foreach (BuildBuyProduct p in Catalog())
                if (KeyString(p.ProductResourceKey) == key.ToUpperInvariant()) return p;
            throw new McpException("unknown product '" + key + "' (use catalog_search)");
        }

        // Create, position and pay for a product without any placement search.
        static GameObject CreateAt(BuildBuyProduct prod, Vector3 pos, Vector3 fwd, bool free)
        {
            Household hh = Household.ActiveHousehold;
            if (hh == null) throw new McpException("no active household");
            int price = (int)prod.Price;
            if (!free && hh.FamilyFunds < price) throw new McpException("not enough money: costs §" + price + ", have §" + hh.FamilyFunds);
            GameObject go = GlobalFunctions.CreateObjectOutOfWorld(prod.ProductResourceKey) as GameObject;
            if (go == null) throw new McpException("the game could not create that object");
            go.SetPosition(pos);
            go.SetForward(fwd);
            go.AddToWorld();
            BuyModePlaced(go);
            if (!free) hh.ModifyFamilyFunds(-price);
            return go;
        }

        // What buy mode does after a drop: modular counters re-join, bookshelves get starter books, etc.
        static void BuyModePlaced(GameObject go)
        {
            try { go.OnHandToolPlacement(); } catch (Exception) { }
        }

        // Put a new product (or an existing object) into a free slot on a surface (desk, counter, table).
        static object PutOn(Args a)
        {
            GameObject surface = FindTarget(a, "surface");
            Slot[] slots = surface.GetContainmentSlots();
            if (slots == null || slots.Length == 0) throw new McpException(Util.ObjectName(surface) + " has no slots");
            bool isNew = a.Has("product");
            BuildBuyProduct prod = isNew ? FindProduct(a.Str("product")) : null;
            Household hh = Household.ActiveHousehold;
            bool free = a.Bool("free", false);
            if (isNew && !free && hh.FamilyFunds < (int)prod.Price)
                throw new McpException("not enough money: costs §" + (int)prod.Price + ", have §" + hh.FamilyFunds);
            GameObject go = isNew ? GlobalFunctions.CreateObjectOutOfWorld(prod.ProductResourceKey) as GameObject
                                  : FindTarget(a, "target");
            if (go == null) throw new McpException("the game could not create that object");
            int want = (int)a.Long("slot", -1);
            for (int i = 0; i < slots.Length; i++)
            {
                if (want >= 0 && i != want) continue;
                if (surface.GetContainedObject(slots[i]) != null) continue;
                if (!go.ParentToSlot(surface, slots[i])) continue;
                if (isNew) go.AddToWorld();
                try { go.OnHandToolPlacementInSlot(surface, slots[i]); } catch (Exception) { }
                if (isNew && !free) hh.ModifyFamilyFunds(-(int)prod.Price);
                Dictionary<string, object> r = Placed(go);
                r["surface"] = Util.ObjectName(surface);
                r["slot"] = i;
                return r;
            }
            if (isNew) go.Destroy();
            throw new McpException("no free slot on " + Util.ObjectName(surface) + " accepted it");
        }

        static Dictionary<string, object> Placed(GameObject go)
        {
            Dictionary<string, object> r = Util.Obj();
            r["id"] = Util.Id(go.ObjectId);
            r["name"] = Util.ObjectName(go);
            r["position"] = Pos(go.Position);
            r["facing"] = Facing(go.ForwardVector);
            r["room"] = go.RoomId;
            r["level"] = go.Level;
            Household hh = Household.ActiveHousehold;
            if (hh != null) r["funds"] = hh.FamilyFunds;
            return r;
        }

        static object PlaceObject(Args a)
        {
            RequireLive();
            BuildBuyProduct prod = FindProduct(a.Str("product"));
            Vector3 pos = new Vector3((float)a.Double("x"), (float)a.Double("y", 0), (float)a.Double("z"));
            if (!a.Has("y")) pos.y = World.GetTerrainHeight(pos.x, pos.z);
            GameObject go = CreateAt(prod, pos, FacingVector(a.Double("facing", 0)), a.Bool("free", false));
            return Placed(go);
        }

        static object ReplaceObject(Args a)
        {
            RequireLive();
            GameObject old = FindTarget(a, "target");
            if (old is Sim || old is Lot) throw new McpException("that cannot be replaced");
            if (old.InUse) throw new McpException(Util.ObjectName(old) + " is in use right now");
            BuildBuyProduct prod = FindProduct(a.Str("product"));
            Vector3 pos = old.Position;
            pos.x += (float)a.Double("dx", 0);
            pos.z += (float)a.Double("dz", 0);
            double facing = Facing(old.ForwardVector) + a.Double("turn", 0);
            string oldName = Util.ObjectName(old);
            int refund = old.Value;
            old.SellBase();
            // SellBase only pays out and hides the object outside buy mode; it stays in the save unless destroyed.
            old.Destroy();
            GameObject go = CreateAt(prod, pos, FacingVector(facing), a.Bool("free", false));
            Dictionary<string, object> r = Placed(go);
            r["replaced"] = oldName;
            r["refund"] = refund;
            r["price"] = (int)prod.Price;
            return r;
        }

        static object SetTransform(Args a)
        {
            RequireLive();
            GameObject o = FindTarget(a, "target");
            if (o is Sim || o is Lot) throw new McpException("use go_here to move sims");
            Vector3 pos = o.Position;
            if (a.Has("x")) pos.x = (float)a.Double("x");
            if (a.Has("y")) pos.y = (float)a.Double("y");
            if (a.Has("z")) pos.z = (float)a.Double("z");
            pos.x += (float)a.Double("dx", 0);
            pos.z += (float)a.Double("dz", 0);
            o.SetPosition(pos);
            if (a.Has("facing")) o.SetForward(FacingVector(a.Double("facing")));
            return Placed(o);
        }

        static object LotLayout(Args a)
        {
            Lot lot = a.Has("lot") ? FindTarget(a, "lot") as Lot : null;
            if (lot == null)
            {
                Household hh = Household.ActiveHousehold;
                lot = hh != null && hh.LotHome != null ? hh.LotHome : Util.ActiveSim().LotCurrent;
            }
            string q = a.Has("query") ? a.Str("query").ToLowerInvariant() : null;
            List<object> objs = new List<object>();
            foreach (GameObject o in lot.GetObjects<GameObject>())
            {
                if (o is Sim) continue;
                string name = Util.ObjectName(o);
                if (q != null && name.ToLowerInvariant().IndexOf(q) < 0 && o.GetType().Name.ToLowerInvariant().IndexOf(q) < 0) continue;
                Dictionary<string, object> d = Util.Obj();
                d["id"] = Util.Id(o.ObjectId);
                d["name"] = name;
                d["type"] = o.GetType().Name;
                d["position"] = Pos(o.Position);
                d["facing"] = Facing(o.ForwardVector);
                d["room"] = o.RoomId;
                d["level"] = o.Level;
                d["value"] = o.Value;
                d["wall"] = o.IsWallObject;
                objs.Add(d);
            }
            Dictionary<string, object> r = Util.Obj();
            r["lot"] = lot.Name;
            r["corner"] = Pos(lot.Position);
            r["objects"] = objs;
            return r;
        }
    }
}

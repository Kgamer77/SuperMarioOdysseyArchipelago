using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using static System.Collections.Specialized.BitVector32;
using System.Net;
using System.Collections;
using Shared;
using Shared.Packet;
using Shared.Packet.Packets;
using Archipelago;
using Archipelago.MultiClient;
using Archipelago.MultiClient.Net;
using Archipelago.MultiClient.Net.Enums;
using Archipelago.MultiClient.Net.Helpers;
using Archipelago.MultiClient.Net.Packets;
using Archipelago.MultiClient.Net.Models;

namespace Server
{
    
    public class APClient
    {
        public ArchipelagoSession session;
        public LoginResult result;
        public ItemInfo lastReceivedItem;
        public Dictionary<string, int> moonsRecieved = new Dictionary<string, int>();
  

        public void Connect(string server, string user, string pass, ushort port)
        {
            session = ArchipelagoSessionFactory.CreateSession(server, port);

            LoginResult result;

            try
            {
                // handle TryConnectAndLogin attempt here and save the returned object to `result`
                result = session.TryConnectAndLogin("Super Mario Odyssey", user, ItemsHandlingFlags.AllItems, null, null, null, pass);
            }
            catch (Exception e)
            {
                result = new LoginFailure(e.GetBaseException().Message);
            }

            if (!result.Successful)
            {
                LoginFailure failure = (LoginFailure)result;
                string errorMessage = $"Failed to Connect to {server} as {user}:";
                foreach (string error in failure.Errors)
                {
                    errorMessage += $"\n    {error}";
                }
                foreach (ConnectionRefusedError error in failure.ErrorCodes)
                {
                    errorMessage += $"\n    {error}";
                }

                Console.WriteLine(errorMessage);
                // Did not connect, show the user the contents of `errorMessage`
            }
            // Successfully connected, `ArchipelagoSession` (assume statically defined as `session` from now on) can now be used to interact with the server and the returned `LoginSuccessful` contains some useful information about the initial connection (e.g. a copy of the slot data as `loginSuccess.SlotData`)
       }



        
        public void send_location(int location)
        {
            session.Locations.CompleteLocationChecks(location);
        }
    }
}

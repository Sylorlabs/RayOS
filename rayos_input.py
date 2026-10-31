#!/usr/bin/env python3
"""
RayOS Input Injector - Simulates mouse input for RayOS desktop interaction

This script injects mouse events into RayOS's MMIO input ring buffer
to allow interaction with the desktop environment.
"""

import sys
import time
import struct
import os

# MMIO constants from core/constants.flash
RAYOS_INPUT_MMIO_BASE = 0xE000C000
RAYOS_MMIO_MAP_BYTES = 65536

# Input ring buffer offsets
INPUT_REG_HEAD = 0
INPUT_REG_TAIL = 4
INPUT_REG_CAPACITY = 8
INPUT_EVENT_BASE = 12
INPUT_EVENT_STRIDE = 16
INPUT_RING_CAPACITY = 64

# Event kinds
INPUT_KIND_MOVE = 0
INPUT_KIND_BUTTON = 1

class RayOSInputInjector:
    def __init__(self):
        self.mmio_data = bytearray(RAYOS_MMIO_MAP_BYTES)
        self._init_ring_buffer()
        
    def _init_ring_buffer(self):
        """Initialize the input ring buffer"""
        # Set head and tail to 0
        self._write32(INPUT_REG_HEAD, 0)
        self._write32(INPUT_REG_TAIL, 0)
        # Set capacity
        self._write32(INPUT_REG_CAPACITY, INPUT_RING_CAPACITY)
        print("RayOS Input Ring Buffer initialized")
        
    def _read32(self, offset):
        """Read a 32-bit value from MMIO"""
        return struct.unpack('<I', self.mmio_data[offset:offset+4])[0]
    
    def _write32(self, offset, value):
        """Write a 32-bit value to MMIO"""
        self.mmio_data[offset:offset+4] = struct.pack('<I', value)
    
    def _ring_index(self, idx, cap):
        """Calculate ring buffer index with wrap-around"""
        if cap < 1:
            return 0
        return idx % cap
    
    def _queue_len(self, head, tail, cap):
        """Calculate number of events in queue"""
        if tail >= head:
            return tail - head
        return cap - head + tail
    
    def inject_event(self, kind, x, y, btn):
        """Inject an input event into the ring buffer"""
        head = self._read32(INPUT_REG_HEAD)
        tail = self._read32(INPUT_REG_TAIL)
        cap = self._read32(INPUT_REG_CAPACITY)
        
        if self._queue_len(head, tail, cap) >= cap:
            print("Input ring buffer full!")
            return False
        
        # Calculate slot position
        slot = self._ring_index(tail, cap)
        event_offset = INPUT_EVENT_BASE + slot * INPUT_EVENT_STRIDE
        
        # Write event data
        self._write32(event_offset + 0, kind)      # kind
        self._write32(event_offset + 4, x)        # x
        self._write32(event_offset + 8, y)        # y  
        self._write32(event_offset + 12, btn)     # button
        
        # Update tail
        next_tail = self._ring_index(tail + 1, cap)
        self._write32(INPUT_REG_TAIL, next_tail)
        
        return True
    
    def move_mouse(self, x, y):
        """Move mouse to position"""
        return self.inject_event(INPUT_KIND_MOVE, x, y, 0)
    
    def click(self, x, y):
        """Click at position (press and release)"""
        self.inject_event(INPUT_KIND_BUTTON, x, y, 1)  # press
        time.sleep(0.01)
        self.inject_event(INPUT_KIND_BUTTON, x, y, 0)  # release
        return True
    
    def drag(self, start_x, start_y, end_x, end_y):
        """Drag from start to end position"""
        self.inject_event(INPUT_KIND_BUTTON, start_x, start_y, 1)  # press
        time.sleep(0.01)
        self.inject_event(INPUT_KIND_MOVE, end_x, end_y, 1)        # move while pressed
        time.sleep(0.01) 
        self.inject_event(INPUT_KIND_BUTTON, end_x, end_y, 0)       # release
        return True
    
    def get_status(self):
        """Get current ring buffer status"""
        head = self._read32(INPUT_REG_HEAD)
        tail = self._read32(INPUT_REG_TAIL)
        cap = self._read32(INPUT_REG_CAPACITY)
        queued = self._queue_len(head, tail, cap)
        return {
            'head': head,
            'tail': tail, 
            'capacity': cap,
            'queued': queued
        }

def demo_sequence(injector):
    """Run a demo sequence of interactions"""
    print("Running demo sequence...")
    
    # Click on launcher (bottom left corner area)
    print("Clicking launcher...")
    injector.click(100, 750)
    time.sleep(0.5)
    
    # Move to center
    print("Moving to center...")
    injector.move_mouse(640, 400)
    time.sleep(0.3)
    
    # Click in center
    print("Clicking center...")
    injector.click(640, 400)
    time.sleep(0.5)
    
    print("Demo sequence complete!")

def interactive_mode(injector):
    """Interactive mode for manual input"""
    print("Interactive mode enabled.")
    print("Commands: move <x> <y>, click <x> <y>, drag <x1> <y1> <x2> <y2>, status, quit")
    
    while True:
        try:
            cmd = input("rayos> ").strip().split()
            if not cmd:
                continue
                
            if cmd[0] == 'quit':
                break
            elif cmd[0] == 'status':
                status = injector.get_status()
                print(f"Status: {status}")
            elif cmd[0] == 'move' and len(cmd) == 3:
                x, y = int(cmd[1]), int(cmd[2])
                injector.move_mouse(x, y)
                print(f"Moved to ({x}, {y})")
            elif cmd[0] == 'click' and len(cmd) == 3:
                x, y = int(cmd[1]), int(cmd[2])
                injector.click(x, y)
                print(f"Clicked at ({x}, {y})")
            elif cmd[0] == 'drag' and len(cmd) == 5:
                x1, y1, x2, y2 = int(cmd[1]), int(cmd[2]), int(cmd[3]), int(cmd[4])
                injector.drag(x1, y1, x2, y2)
                print(f"Dragged from ({x1}, {y1}) to ({x2}, {y2})")
            else:
                print("Unknown command")
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")

def main():
    injector = RayOSInputInjector()
    
    if len(sys.argv) > 1 and sys.argv[1] == 'demo':
        demo_sequence(injector)
    else:
        interactive_mode(injector)

if __name__ == '__main__':
    main()
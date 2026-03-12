import numpy as np
import open3d as o3d


def main():
    print("=== Sparse Cloud Distance Measurement Tool ===\n")
    
    # Load point cloud
    ply_path = "recon_sparse/sparse.ply"
    pcd = o3d.io.read_point_cloud(ply_path)
    points = np.asarray(pcd.points)
    print(f"Loaded {len(points)} points\n")
    
    scale = None
    
    while True:
        print("\n" + "="*60)
        if scale is None:
            print("CALIBRATION MODE")
            print("="*60)
            print("Pick 2 points with KNOWN distance apart")
        else:
            print("MEASUREMENT MODE")
            print("="*60)
            print("Pick 2 points to measure distance")
        
        print("\nInstructions:")
        print("  1. The viewer will open")
        print("  2. Shift + Left Click to pick points")
        print("  3. Close the window when you've picked 2 points")
        print("\nPress Enter to open viewer...")
        input()
        
        # Create visualizer with editing
        vis = o3d.visualization.VisualizerWithEditing()
        vis.create_window("Pick 2 Points (Shift+Click)", width=1400, height=900)
        vis.add_geometry(pcd)
        vis.run()
        
        # Get picked points
        picked = vis.get_picked_points()
        vis.destroy_window()
        
        if len(picked) < 2:
            print(f"\n!! Only {len(picked)} point(s) picked. Need 2 points.")
            retry = input("Try again? (y/n): ").strip().lower()
            if retry != 'y':
                break
            continue
        
        # picked is already a list of integers (indices)
        idx1 = picked[0]
        idx2 = picked[1]
        
        p1, p2 = points[idx1], points[idx2]
        dist = np.linalg.norm(p1 - p2)
        
        print(f"\nPoint 1: index {idx1}, position ({p1[0]:.3f}, {p1[1]:.3f}, {p1[2]:.3f})")
        print(f"Point 2: index {idx2}, position ({p2[0]:.3f}, {p2[1]:.3f}, {p2[2]:.3f})")
        print(f"Model distance: {dist:.6f} units")
        
        if scale is None:
            # Calibration
            real_cm = float(input("\nEnter the REAL distance between these points (in cm): "))
            scale = real_cm / dist
            print(f"\nCalibration complete!")
            print(f"  Scale: {scale:.6f} cm per unit")
            
            cont = input("\nContinue to measure distances? (y/n): ").strip().lower()
            if cont != 'y':
                break
        else:
            # Measurement
            distance_cm = dist * scale
            print(f"\nDISTANCE: {distance_cm:.2f} cm")
            
            cont = input("\nMeasure another distance? (y/n): ").strip().lower()
            if cont != 'y':
                break
    
    print("\nDone!")


if __name__ == "__main__":
    main()
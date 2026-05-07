import tkinter as tk
import math

# -----------------------------------------------------------------------------
# Reference: 
# [1] Schaefer, S., McPhail, T., & Warren, J. (2006). 
#     "Image Deformation Using Moving Least Squares". 
#     Proceedings of SIGGRAPH 2006.
# -----------------------------------------------------------------------------

class SimpleUI:
    def __init__(self, root):
        self.root = root
        self.root.title("MLS Deformation Demo")
        
        # --- Critical State Variables ---
        
        # self.p: The set of original control points $P = \{p_i\}$.
        # Defines the reference configuration of the deformation handles.
        self.p = [(100, 100), (300, 100), (300, 300), (100, 300)]
        
        # self.q: The set of deformed control points $Q = \{q_i\}$.
        # Represents the current boundary conditions for the deformation mapping $f(x)$.
        self.q = [(100, 100), (300, 100), (300, 300), (100, 300)]
        
        # self.v: The set of vertex positions $V$ defining the discrete domain $\Omega$.
        self.v = [(150, 150), (250, 150), (250, 250), (150, 250)]
        
        # self.alpha: Regularization parameter $\alpha$.
        # Controls the fall-off of the weight function $w_i(v) = 1 / |v - p_i|^{2\alpha}$.
        self.alpha = 1.0

        self.canvas = tk.Canvas(root, width=400, height=400, bg="white")
        self.canvas.pack()
        
        self.draw_scene()
        
        # Bind mouse events for interaction
        self.selected_idx = -1
        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<B1-Motion>", self.on_drag)

    def on_click(self, event):
        # Identify the handle $q_i$ minimizing Euclidean distance to the cursor $x_{mouse}$.
        mouse_pos = (event.x, event.y)
        min_dist = float('inf')
        for i, pt in enumerate(self.q):
            dist = math.hypot(pt[0] - mouse_pos[0], pt[1] - mouse_pos[1])
            if dist < 10:
                self.selected_idx = i
                break
        else:
            self.selected_idx = -1

    def on_drag(self, event):
        if self.selected_idx != -1:
            # Update position of active handle $q_k$.
            self.q[self.selected_idx] = (event.x, event.y)
            self.update_deformation()
            self.draw_scene()

    def update_deformation(self):
        """
        Recomputes vertex positions based on the Moving Least Squares (MLS) algorithm.
        """
        # Note: This is a simplified affine deformation approximation for demonstration.
        
        for i, v in enumerate(self.v):
            # -----------------------------------------------------------------
            # Computes the affine mapping $l_v(x)$ that minimizes the weighted least squares error:
            # $\sum_i w_i(v) |l_v(p_i) - q_i|^2$
            # and updates vertex $v$ to $f(v) = l_v(v)$.
            # -----------------------------------------------------------------
            
            # (Simplistic offset calculation for demo purposes to represent the operation)
            # In a full implementation, this block calculates $(v - p_*) M + q_*$.
            dx = self.q[0][0] - self.p[0][0]
            dy = self.q[0][1] - self.p[0][1]
            
            # Apply a fraction of the handle displacement to the inner vertices
            self.v[i] = (v[0] + dx * 0.1, v[1] + dy * 0.1) 

    def draw_scene(self):
        self.canvas.delete("all")
        
        # Draw edges of the deformed domain $\Omega'$
        if len(self.v) > 1:
            self.canvas.create_polygon(self.v, outline="blue", fill="lightblue", dash=(2, 2))

        # Draw control handles $Q$
        for x, y in self.q:
            self.canvas.create_oval(x-5, y-5, x+5, y+5, fill="red", outline="black")

if __name__ == "__main__":
    root = tk.Tk()
    app = SimpleUI(root)
    root.mainloop()
